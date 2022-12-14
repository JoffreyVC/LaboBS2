/** sqdfqsd
 * \author Mathieu Erbas
 */

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include "sbuffer.h"
#include "config.h"

#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

// ^ Element in de Buffer
typedef struct sbuffer_node {
    // stond eerst een link naar de vorige node maar dat werkt in ons nadeel als we altijd van langs achter willen kijken
    struct sbuffer_node* next;   // pointer naar de volgende
    sensor_data_t data;          // data in een node van de buffer
    bool stored;          // is storage manager al geweest?
    bool datamanaged;     // is data manager al geweest?
    pthread_mutex_t node_mutex;  // mutex nodig om te kijken of beiden booleans true zijn
} sbuffer_node_t;

// ^ De Buffer
struct sbuffer {
    sbuffer_node_t* head;  // kop waar elementen worden toegevoegd door connection manager
    sbuffer_node_t* tail;  // staart waar readers elementen van halen
    bool closed;
    pthread_rwlock_t rw_lock;
    unsigned long id_readers[2];  // storage, data - manager
    pthread_cond_t bufferIsExpanded;  // condition variabele waarop we kunnen wachten
    pthread_mutex_t mutex;  // hulp voor condition variabele ... ambetant maar verplicht ...
};

// // todo https://stackoverflow.com/questions/2699993/using-pthread-condition-variable-with-rwlock dit is fucked but there is no other way

// ---------------------------------------------------------  MAKEN EN INITIALISEREN  ----------------------------------------------------------

// maak node
static sbuffer_node_t* create_node(const sensor_data_t* data) {     // maak een node voor in de buffer
    sbuffer_node_t* node = malloc(sizeof(*node));
    *node = (sbuffer_node_t){
        .data = *data,
        .next = NULL,
        .stored = false,
        .datamanaged = false
    };
    ASSERT_ELSE_PERROR(pthread_mutex_init(&node->node_mutex, NULL) == 0);  // initialisatie van mutex per node
    return node;
}

// maak buffer
sbuffer_t* sbuffer_create() {
    sbuffer_t* buffer = malloc(sizeof(sbuffer_t));
    assert(buffer != NULL);  // should never fail due to optimistic memory allocation

    buffer->head = NULL;
    buffer->tail = NULL;
    buffer->closed = false;

    ASSERT_ELSE_PERROR(pthread_rwlock_init(&buffer->rw_lock, NULL) == 0);  // initialisatie RWlock
    ASSERT_ELSE_PERROR(pthread_cond_init(&(buffer->bufferIsExpanded), NULL) == 0);  // initialisatie CD variabele om wait en signal uit te voeren
    ASSERT_ELSE_PERROR(pthread_mutex_init(&buffer->mutex, NULL) == 0);  // initialisatie onbelangrijke mutex

    return buffer;
}
void printBuffer(sbuffer_t* buffer) {
    if (buffer->head == NULL) {
        printf("Buffer is leeg");
    }
    else {
        sbuffer_node_t* node = buffer->tail;
        printf("Buffer: ");
        while (node->next != NULL){
            printf("%f", node->data.value);
            printf(" ");
            node = node->next;
        }
    }
}



// verwijder buffer
void sbuffer_destroy(sbuffer_t* buffer) {       // vrijgeven van de buffer
    assert(buffer);
    // make sure it's empty
    assert(buffer->head == buffer->tail);
    // // ASSERT_ELSE_PERROR(pthread_mutex_destroy(&buffer->mutex) == 0);
    free(buffer);
}

/* -> verwoven in de nodige functies zelf
void sbuffer_lock(sbuffer_t* buffer) {      // lock de buffer
    assert(buffer);
    // // ASSERT_ELSE_PERROR(pthread_mutex_lock(&buffer->mutex) == 0);
}
void sbuffer_unlock(sbuffer_t* buffer) {        // unlock de buffer
    assert(buffer);
    // // ASSERT_ELSE_PERROR(pthread_mutex_unlock(&buffer->mutex) == 0);
}
*/

void sbuffer_identify_readers(sbuffer_t* buffer, bool isDataManager){
    if(isDataManager) buffer->id_readers[1] = pthread_self();
    else buffer->id_readers[0] = pthread_self();
}

void sbuffer_close(sbuffer_t* buffer) {     // close de buffer
    assert(buffer);

    // kan blocken, maar we moeten het zeker hebben ...
    pthread_rwlock_wrlock(&(buffer->rw_lock));

    buffer->closed = true;

    // geef rw lock terug vrij
    pthread_rwlock_unlock(&(buffer->rw_lock));
}

// ---------------------------------------------------------  CHECKS  ----------------------------------------------------------


bool sbuffer_is_empty(sbuffer_t* buffer) {      // is de buffer leeg?
    assert(buffer);
    return buffer->head == NULL;
}

bool sbuffer_is_closed(sbuffer_t* buffer) {     // is de buffer closed?
    assert(buffer);
    return buffer->closed;
}

// ---------------------------------------------------------  TOEVOEGEN  ----------------------------------------------------------


int sbuffer_insert_first(sbuffer_t* buffer, sensor_data_t const* data) {        // insert aan het begin van de buffer
    assert(buffer && data);
    // ? de check hoeft denk ik niet met synchronisatie te gebeuren
    if (buffer->closed)
        return SBUFFER_FAILURE;  // kan niet als hij toe is

    // ? de nieuwe node maken kan ook zonder synchronisatie gebeuren denk ik
    sbuffer_node_t* node = create_node(data);
    assert(node->next == NULL);

    pthread_rwlock_wrlock(&(buffer->rw_lock));
    pthread_mutex_lock(&(buffer->mutex));

    // insert it - we zijn alleen in de buffer
    // we voegen onze nieuwe node toe aan de kop van de buffer
    if (buffer->head != NULL)
        buffer->head->next = node;
    buffer->head = node;
    if (buffer->tail == NULL)
        buffer->tail = node;

    //printf("einde insert \n");
    printBuffer(buffer);
    printf("\n");

    // geef rw lock terug vrij
    pthread_mutex_unlock(&(buffer->mutex));
    pthread_rwlock_unlock(&(buffer->rw_lock));
    //printf("broadcast \n");
    pthread_cond_broadcast(&(buffer->bufferIsExpanded));  // maak de readers wakker die hier vastzaten

    return SBUFFER_SUCCESS;
}

// ---------------------------------------------------------  VERWIJDEREN  ----------------------------------------------------------


sensor_data_t sbuffer_remove_last(sbuffer_t* buffer) {      // geef laatste van buffer terug en verwijder
    assert(buffer);
    // assert(buffer->head != NULL);
    // kan blocken, maar we moeten het zeker hebben ... - alternatief is met de try functie
    pthread_rwlock_rdlock(&(buffer->rw_lock));
    bool isDataManager = pthread_self() == buffer->id_readers[1];
    bool isStorageManager = !isDataManager;

    // zoek een node om te lezen
    bool gevonden = false;
    sbuffer_node_t* used_node;
    while (!gevonden){  // we moeten sws een node vinden, desnoods wachten we even

        // bestaat de buffer? -> zoja ...
        if (buffer->tail != NULL){  
            used_node = buffer->tail;  // start vanachter te zoeken

            while (!gevonden){ // zolang geen bruikbare gevonden en er nog een volgende is
                // we zijn data manager en we hebben onze job nog niet gedaan
                if (isDataManager && !used_node->datamanaged) gevonden = true;
                // we zijn storage manager en we hebben onze job nog niet gedaan
                else if (isStorageManager && !used_node->stored) gevonden = true;

                if (gevonden){
                    if (pthread_mutex_trylock(&(used_node->node_mutex)) != 0) {
                        gevonden = false;                                       
                    } else {
                        // printf("we hebben een node gevonden en gelockt %d \n", isDataManager);
                    }
                } 
                if (used_node->next == NULL){
                    break;
                }
                if (!gevonden) used_node = used_node->next;


            } 
        }

        // er is geen node meer om te lezen -> slaap en geef lock terug
        if (!gevonden){
            //printf("we gaan even wachten %d \n", isDataManager);
            pthread_mutex_lock(&(buffer->mutex));

            // geef rw lock terug vrij
            pthread_rwlock_unlock(&(buffer->rw_lock));
            pthread_cond_wait(&(buffer->bufferIsExpanded), &(buffer->mutex));

            //printf("ik ben terug wakker %d \n", isDataManager);
            pthread_mutex_unlock(&(buffer->mutex));
            pthread_rwlock_rdlock(&(buffer->rw_lock));
        }
    }

    // lees van node
    sensor_data_t ret = used_node->data;
    if (isDataManager) used_node->datamanaged = true;
    else used_node->stored = true;

    printf("%d %d \n", used_node->stored, used_node->datamanaged);

    // geef node en buffer vrij
    pthread_mutex_unlock(&(used_node->node_mutex));
    pthread_rwlock_unlock(&(buffer->rw_lock));

    if (used_node->stored && used_node->datamanaged){
        // neem write lock voor verwijderen
        pthread_rwlock_wrlock(&(buffer->rw_lock));

        printf("te removen node: ");
        printf(" %f", used_node->data.value);

        sbuffer_node_t* temp = buffer->tail;
        buffer->tail = buffer->tail->next;
        free(temp);

        // Geef buffer terug vrij
        printf("einde verwijderen \n");
        pthread_rwlock_unlock(&(buffer->rw_lock));
    } 

    //printf("einde verwijderen \n");

    return ret;
}
