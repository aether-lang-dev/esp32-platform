#include "runner_bundle.h"
#include "psa/crypto.h"
#include <stdbool.h>
#include <string.h>

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static unsigned u16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static int identifier(const unsigned char *p, size_t n) {
    if (!n || n > 64) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = p[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}
size_t runner_bundle_encoded_size(const unsigned char *data, size_t available) {
    if (!data || available < 24 || memcmp(data, "RXAEUP01", 8) || !u32(data+8) ||
        (u32(data+12) & ~RUNNER_SIGNED_GRANTS)) return 0;
    size_t k=u16(data+16), n=u16(data+18), source=u32(data+20);
    if (!k || k>64 || !n || n>64 || !source || source>SCRIPT_LIMIT) return 0;
    return 24+k+n+source+64;
}
/* Hash the authenticated content, omitting version and signature. */
static bool fingerprint(const unsigned char *data, size_t length, char out[65]) {
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    unsigned char digest[32];
    size_t count;
    if (psa_hash_setup(&operation, PSA_ALG_SHA_256) != PSA_SUCCESS ||
        psa_hash_update(&operation, data, 8) != PSA_SUCCESS ||
        psa_hash_update(&operation, data + 12, length - 12) != PSA_SUCCESS ||
        psa_hash_finish(&operation, digest, sizeof digest, &count) != PSA_SUCCESS ||
        count != sizeof digest) {
        (void)psa_hash_abort(&operation);
        return false;
    }
    const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof digest; ++i) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
    return true;
}

int runner_bundle_verify(const unsigned char *data, size_t size,
                         const RunnerTrustedKey *keys, size_t key_count,
                         RunnerBundle *result) {
    if (!data || !keys || !result || size < 24 + 1 + 1 + 1 + 64 || size > RUNNER_BUNDLE_LIMIT)
        return -1;
    if (memcmp(data, "RXAEUP01", 8)) return -1;
    uint32_t version = u32(data + 8), grants = u32(data + 12);
    size_t key_length = u16(data + 16), name_length = u16(data + 18), source_length = u32(data + 20);
    if (!version || (grants & ~RUNNER_SIGNED_GRANTS) || !key_length || key_length > 64 ||
        !name_length || name_length > 64 || !source_length || source_length > SCRIPT_LIMIT)
        return -1;
    size_t signed_length = 24 + key_length + name_length + source_length;
    if (size != signed_length + 64) return -1;
    const unsigned char *key_id = data + 24;
    const unsigned char *name = key_id + key_length;
    const unsigned char *source = name + name_length;
    if (!identifier(key_id, key_length) || !identifier(name, name_length) || memchr(source, 0, source_length))
        return -1;
    const RunnerTrustedKey *trusted = NULL;
    for (size_t i = 0; i < key_count; ++i) {
        if (keys[i].id && strlen(keys[i].id) == key_length && !memcmp(keys[i].id, key_id, key_length)) {
            /* Duplicate ids in host configuration are ambiguous: fail closed. */
            if (trusted) return -1;
            trusted = &keys[i];
        }
    }
    if (!trusted || !trusted->public_key || psa_crypto_init() != PSA_SUCCESS) return -1;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t handle = 0;
    psa_status_t status = psa_import_key(&attributes, trusted->public_key, 65, &handle);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) return -1;
    status = psa_verify_message(handle, PSA_ALG_ECDSA(PSA_ALG_SHA_256), data, signed_length,
                                data + signed_length, 64);
    psa_status_t cleanup = psa_destroy_key(handle);
    if (status != PSA_SUCCESS || cleanup != PSA_SUCCESS) return -1;
    RunnerBundle parsed = {.version = version, .grants = grants, .source = source,
                           .source_length = source_length};
    memcpy(parsed.key_id, key_id, key_length);
    memcpy(parsed.name, name, name_length);
    if (!fingerprint(data, signed_length, parsed.fingerprint)) return -1;
    *result = parsed;
    return 0;
}
