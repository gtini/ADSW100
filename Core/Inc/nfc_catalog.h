/* Contratto NFC manuale: incrementare NFC_SCHEMA_REVISION per modifiche incompatibili. */
#ifndef NFC_CATALOG_H
#define NFC_CATALOG_H
#include <stdint.h>
#define NFC_SCHEMA_REVISION 1u
#define NFC_CATALOG_MANUFACTURER 0x17FAu
#define NFC_CATALOG_MODEL 0x0060u
#define NFC_CATALOG_CATEGORY 0x0601u
typedef struct
{
    uint16_t pid;
    uint8_t selector_size, value_size, type;
    uint32_t selector;
} nfc_field_t;
static const nfc_field_t nfc_fields[] =
{
    {0x00F0u, 0u, 2u, 2u, 0x00000000u},
    {0x0703u, 4u, 1u, 7u, 0x00000001u},
    {0x0706u, 4u, 5u, 10u, 0x00000001u},
    {0x070Au, 0u, 8u, 11u, 0x00000000u},
    {0x070Bu, 1u, 4u, 9u, 0x00000000u},
    {0x070Bu, 1u, 4u, 9u, 0x00000001u},
    {0x9100u, 0u, 1u, 1u, 0x00000000u},
    {0x9101u, 0u, 1u, 1u, 0x00000000u},
    {0x9102u, 0u, 2u, 2u, 0x00000000u},
    {0x9103u, 0u, 2u, 2u, 0x00000000u},
    {0x9104u, 0u, 1u, 1u, 0x00000000u},
    {0x9105u, 0u, 1u, 1u, 0x00000000u},
    {0x9106u, 0u, 1u, 1u, 0x00000000u},
    {0x9107u, 0u, 1u, 7u, 0x00000000u},
    {0x9108u, 0u, 1u, 1u, 0x00000000u},
};
#define NFC_FIELD_COUNT (sizeof nfc_fields / sizeof nfc_fields[0])
#endif
