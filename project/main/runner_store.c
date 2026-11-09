#include "runner_store.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <string.h>

#define NONE 255u
#define RECORD_SIZE 84
/* Explicit byte encoding in NVS: RXS1, highest u32, active-version u32,
 * active-slot u8, trial-slot u8, reserved[2], name[65], reserved[3]. */
typedef struct {
    uint32_t highest, active_version;
    unsigned active, trial;
    char name[65];
    /* RAM only. Boot rebuilds the active hash from the verified flash bundle. */
    char active_fingerprint[65], trial_fingerprint[65];
} State;
typedef struct {
    State state;
    bool fault;
    nvs_handle_t storage;
} Store;
static Store stores[] = {
    [RUNNER_STORE_APP] = {.fault = true},
    [RUNNER_STORE_LABELS] = {.fault = true},
};
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void put32(unsigned char *p, uint32_t n) {
    for (int i=0;i<4;++i) { p[i]=(unsigned char)n; n>>=8; }
}
static State snapshot(const Store *store) {
    portENTER_CRITICAL(&lock); State copy=store->state; portEXIT_CRITICAL(&lock); return copy;
}
static void failed(Store *store) { portENTER_CRITICAL(&lock); store->fault=true; portEXIT_CRITICAL(&lock); }
static int save(Store *store, const State *next) {
    unsigned char bytes[RECORD_SIZE]={0};
    memcpy(bytes,"RXS1",4); put32(bytes+4,next->highest); put32(bytes+8,next->active_version);
    bytes[12]=(unsigned char)next->active; bytes[13]=(unsigned char)next->trial;
    memcpy(bytes+16,next->name,sizeof next->name);
    if (nvs_set_blob(store->storage,"state",bytes,sizeof bytes)!=ESP_OK || nvs_commit(store->storage)!=ESP_OK) {
        failed(store); return -1;
    }
    portENTER_CRITICAL(&lock); store->state=*next; portEXIT_CRITICAL(&lock); return 0;
}
static bool decode(const unsigned char *bytes, State *out) {
    if (memcmp(bytes,"RXS1",4) || bytes[14] || bytes[15] || bytes[81] || bytes[82] || bytes[83]) return false;
    State copy={.highest=get32(bytes+4),.active_version=get32(bytes+8),.active=bytes[12],.trial=bytes[13]};
    memcpy(copy.name,bytes+16,sizeof copy.name);
    if (!memchr(copy.name,0,sizeof copy.name)) return false;
    size_t n=strlen(copy.name);
    for (size_t i=0;i<n;++i) {
        unsigned char c=(unsigned char)copy.name[i];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return false;
    }
    for (size_t i=n;i<sizeof copy.name;++i) if (copy.name[i]) return false;
    if ((!copy.highest)!=(!n) || copy.active_version>copy.highest ||
        (copy.active!=NONE && copy.active>1) || (copy.trial!=NONE && copy.trial>1) ||
        (copy.active==NONE ? copy.active_version!=0 : copy.active_version==0) ||
        (copy.trial!=NONE && (!copy.highest || copy.trial==copy.active))) return false;
    *out=copy; return true;
}
static const esp_partition_t *slot(RunnerStoreId id, unsigned index) {
    const char *label = id == RUNNER_STORE_LABELS ? (index == 0 ? "labels_a" : "labels_b") :
                                                   (index == 0 ? "script_a" : "script_b");
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, label);
}
int runner_store_init_at(RunnerStoreId id) {
    Store *store = &stores[id];
    if (id == RUNNER_STORE_LABELS) {
        const esp_partition_t *a = slot(id, 0), *b = slot(id, 1);
        if (!a || !b || a->size < RUNNER_BUNDLE_LIMIT || b->size < RUNNER_BUNDLE_LIMIT) return -1;
    }
    const char *namespace = id == RUNNER_STORE_LABELS ? "rexx-labels" : "rexx-runner";
    store->state=(State){.active=NONE,.trial=NONE};
    /* Never erase NVS automatically: that would silently reset replay state. */
    if (nvs_flash_init()!=ESP_OK || nvs_open(namespace,NVS_READWRITE,&store->storage)!=ESP_OK) return -1;
    unsigned char bytes[RECORD_SIZE]; size_t size=sizeof bytes;
    esp_err_t status=nvs_get_blob(store->storage,"state",bytes,&size);
    State loaded={.active=NONE,.trial=NONE};
    if (status!=ESP_ERR_NVS_NOT_FOUND && (status!=ESP_OK || size!=sizeof bytes || !decode(bytes,&loaded))) return -1;
    portENTER_CRITICAL(&lock); store->state=loaded; store->fault=false; portEXIT_CRITICAL(&lock);
    /* A reboot during trial rejects it, preserving both the previous active
     * release and the highest accepted version. It is never retried on boot. */
    if (loaded.trial!=NONE) { loaded.trial=NONE; return save(store, &loaded); }
    return 0;
}
void runner_store_info_at(RunnerStoreId id, RunnerStoreInfo *info) {
    const Store *store = &stores[id];
    portENTER_CRITICAL(&lock);
    *info=(RunnerStoreInfo){.active_version=store->state.active_version,.highest_version=store->state.highest,
        .trial_version=store->state.trial==NONE?0:store->state.highest,.active_slot=store->state.active==NONE?-1:(int)store->state.active,.fault=store->fault};
    memcpy(info->active_fingerprint, store->state.active_fingerprint, sizeof info->active_fingerprint);
    portEXIT_CRITICAL(&lock);
}
static bool accepts(RunnerStoreId id, const RunnerBundle *bundle) {
    bool labels = strcmp(bundle->name, RUNNER_LABELS_BUNDLE) == 0;
    return id == RUNNER_STORE_LABELS ? labels && !bundle->grants : !labels;
}
int runner_store_offer_at(RunnerStoreId id, const RunnerBundle *candidate) {
    const Store *store = &stores[id];
    RunnerStoreInfo info; runner_store_info_at(id, &info);
    if (info.fault) return -1;
    State current=snapshot(store);
    if (!accepts(id, candidate) || current.trial!=NONE || candidate->version<=current.highest ||
        (current.highest && strcmp(current.name,candidate->name))) return 1;
    return 0;
}
int runner_store_stage_at(RunnerStoreId id, unsigned char *bytes, size_t length,
                          const RunnerTrustedKey *keys, size_t count, RunnerBundle *parsed) {
    Store *store = &stores[id];
    RunnerBundle before;
    if (runner_bundle_verify(bytes,length,keys,count,&before)!=0 || runner_store_offer_at(id, &before)!=0) return -1;
    State next=snapshot(store); unsigned target=next.active==0?1:0;
    const esp_partition_t *partition=slot(id, target);
    unsigned char signature[64]; memcpy(signature,bytes+length-64,64);
    if (!partition || length>partition->size || esp_partition_erase_range(partition,0,partition->size)!=ESP_OK ||
        esp_partition_write(partition,0,bytes,length)!=ESP_OK) return -1;
    memset(bytes,0,length);
    if (esp_partition_read(partition,0,bytes,length)!=ESP_OK || memcmp(signature,bytes+length-64,64) ||
        runner_bundle_verify(bytes,length,keys,count,parsed)!=0 || parsed->version!=before.version ||
        strcmp(parsed->name,before.name)) return -1;
    next.highest=parsed->version; next.trial=target;
    memcpy(next.name,parsed->name,sizeof next.name);
    memcpy(next.trial_fingerprint, parsed->fingerprint, sizeof next.trial_fingerprint);
    return save(store, &next);
}
int runner_store_finish_at(RunnerStoreId id, bool healthy) {
    Store *store = &stores[id];
    State next=snapshot(store);
    if (next.trial==NONE) return -1;
    if (healthy) {
        next.active=next.trial;
        next.active_version=next.highest;
        memcpy(next.active_fingerprint, next.trial_fingerprint, sizeof next.active_fingerprint);
    }
    next.trial_fingerprint[0] = 0;
    next.trial=NONE;
    return save(store, &next);
}
unsigned char *runner_store_load_active_at(RunnerStoreId id, const RunnerTrustedKey *keys, size_t count,
                                          RunnerBundle *parsed) {
    Store *store = &stores[id];
    RunnerStoreInfo info; runner_store_info_at(id, &info);
    if (info.fault || info.active_slot<0) return NULL;
    State current=snapshot(store);
    portENTER_CRITICAL(&lock);
    store->state.active_fingerprint[0] = 0;
    portEXIT_CRITICAL(&lock);
    const esp_partition_t *partition=slot(id, current.active);
    unsigned char header[24];
    if (!partition || esp_partition_read(partition,0,header,sizeof header)!=ESP_OK) return NULL;
    size_t length=runner_bundle_encoded_size(header,sizeof header);
    if (!length || length>partition->size) return NULL;
    unsigned char *bytes=malloc(length+1);
    if (!bytes) return NULL;
    if (esp_partition_read(partition,0,bytes,length)!=ESP_OK || runner_bundle_verify(bytes,length,keys,count,parsed)!=0 ||
        !accepts(id, parsed) || parsed->version!=current.active_version || strcmp(parsed->name,current.name)) { free(bytes); return NULL; }
    portENTER_CRITICAL(&lock);
    memcpy(store->state.active_fingerprint, parsed->fingerprint, sizeof store->state.active_fingerprint);
    portEXIT_CRITICAL(&lock);
    return bytes;
}
