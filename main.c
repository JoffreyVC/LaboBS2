/**
 * \author Mathieu Erbas
 */

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include "config.h"
#include "connmgr.h"
#include "datamgr.h"
#include "sbuffer.h"
#include "sensor_db.h"

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wait.h>

static int print_usage() {
    printf("Usage: <command> <port number> \n");
    return -1;
}

static void* datamgr_run(void* buffer) {        // MAIN_LOOP VOOR DE DATA MANAGER
    datamgr_init();

    // datamgr loop
    while (true) {
        sbuffer_lock(buffer);
        if (!sbuffer_is_empty(buffer)) {  // buffer niet leeg
            sensor_data_t data = sbuffer_remove_last(buffer);  // neem laatste data
            datamgr_process_reading(&data); // bekijk data
            // everything nice & processed
        } else if (sbuffer_is_closed(buffer)) {  // buffer is both empty & closed: there will never be data again
            sbuffer_unlock(buffer);
            break;
        }
        // give the others a chance to lock the mutex
        sbuffer_unlock(buffer);
    }
    // einde programma
    datamgr_free();  
    return NULL;
}

static void* storagemgr_run(void* buffer) {     // MAIN_LOOP VOOR DE STORAGE MANAGER
    DBCONN* db = storagemgr_init_connection(1);
    assert(db != NULL);

    // storagemgr loop
    while (true) {
        sbuffer_lock(buffer);
        if (!sbuffer_is_empty(buffer)) {  // als buffer niet leeg is
            sensor_data_t data = sbuffer_remove_last(buffer);
            storagemgr_insert_sensor(db, data.id, data.value, data.ts);
            // everything nice & processed
        } else if (sbuffer_is_closed(buffer)) {  // buffer is both empty & closed: there will never be data again
            sbuffer_unlock(buffer);
            break;
        }
        // give the others a chance to lock the mutex
        sbuffer_unlock(buffer);
    }
    // einde van programma
    storagemgr_disconnect(db);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2)              // geen idee wat dit boeltje is ma mss boeit het nie
        return print_usage();
    char* strport = argv[1];
    char* error_char = NULL;
    int port_number = strtol(strport, &error_char, 10);
    if (strport[0] == '\0' || error_char[0] != '\0')
        return print_usage();

    sbuffer_t* buffer = sbuffer_create();  // maak een buffer

    pthread_t datamgr_thread;  // data manager thread
    ASSERT_ELSE_PERROR(pthread_create(&datamgr_thread, NULL, datamgr_run, buffer) == 0);

    pthread_t storagemgr_thread;  // storage manager thread
    ASSERT_ELSE_PERROR(pthread_create(&storagemgr_thread, NULL, storagemgr_run, buffer) == 0);

    // main server loop
    connmgr_listen(port_number, buffer);  // connection manager maken

    sbuffer_lock(buffer);
    sbuffer_close(buffer);
    sbuffer_unlock(buffer);

    pthread_join(datamgr_thread, NULL);  // laat threads hier op elkaar wachten
    pthread_join(storagemgr_thread, NULL);  // same

    sbuffer_destroy(buffer);  // verwijder buffer

    wait(NULL);

    return 0;
}
