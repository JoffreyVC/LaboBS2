/**
 * \author Mathieu Erbas
 * 
// ! Red (!) - werkt nog niet - problemen
// ? Blue (?) - klopt dit??
// * Green (*) - haha het werkt
// ^ Yellow (^) - statement
// & Pink (&)
// ~ Purple (~)
// todo Mustard (todo)
 */

// ^ https://doc.riot-os.org/pthread__mutex_8h.html voor uitleg over synchronization primitives
// ^ https://en.cppreference.com/w/c/atomic voor atomic vars

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
#include <stdatomic.h>  // voor atomic operaties
#include <sys/stat.h>
#include <sys/types.h>
#include <wait.h>

// todo: @Jof download extensions: colorful comments en clangd (en pdf viewer)

static int print_usage() {
    printf("Usage: <command> <port number> \n");
    return -1;
}

static void* datamgr_run(void* buffer) {        // ^ LOOP VOOR DE DATA MANAGER
    datamgr_init();
    sbuffer_identify_readers(buffer, true);

    while (true) {
        sensor_data_t data = sbuffer_remove_last(buffer);  // neem laatste data
        datamgr_process_reading(&data); // bekijk data

        /**  GEEN LOCK EN UNLOCK MEER IN LOOPS - UNDER THE HOOD (zie buffer.h)

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
        **/
    }
    // einde programma
    datamgr_free();  
    return NULL;
}

static void* storagemgr_run(void* buffer) {     // ^ LOOP VOOR DE STORAGE MANAGER
    DBCONN* db = storagemgr_init_connection(1);
    assert(db != NULL);
    sbuffer_identify_readers(buffer, false);

    while (true) {
        sensor_data_t data = sbuffer_remove_last(buffer);
        storagemgr_insert_sensor(db, data.id, data.value, data.ts);

        /** GEEN LOCK EN UNLOCK MEER IN LOOPS - UNDER THE HOOD (zie buffer.h)
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
        **/
    }
    // einde van programma
    storagemgr_disconnect(db);
    return NULL;
}

// --------------------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc != 2)              // geen idee wat dit boeltje is ma mss boeit het nie
        return print_usage();
    char* strport = argv[1];
    char* error_char = NULL;
    int port_number = strtol(strport, &error_char, 10);
    if (strport[0] == '\0' || error_char[0] != '\0')
        return print_usage();

    // ^ Buffer
    sbuffer_t* buffer = sbuffer_create();  // maak een buffer

    // ^ DataManager
    pthread_t datamgr_thread;  // data manager thread
    ASSERT_ELSE_PERROR(pthread_create(&datamgr_thread, NULL, datamgr_run, buffer) == 0);

    // ^ StorageManager
    pthread_t storagemgr_thread;  // storage manager thread
    ASSERT_ELSE_PERROR(pthread_create(&storagemgr_thread, NULL, storagemgr_run, buffer) == 0);

    // ^ ConnectionManager
    connmgr_listen(port_number, buffer);  // deze thread  gaat verder in de connection manager

    // // sbuffer_lock(buffer);
    sbuffer_close(buffer);
    // // sbuffer_unlock(buffer);

    pthread_join(datamgr_thread, NULL);  // connection manager thread wacht hier op datamanager
    pthread_join(storagemgr_thread, NULL);  // en hier op storagemanager

    sbuffer_destroy(buffer);  // verwijder buffer

    wait(NULL);

    return 0;
}
