// pkcs8.cc
//
// RSA private key reading/writing

#include <cstdio>
#include <stdexcept>
#include "pkcs8.hpp"

#include <gmp.h>

class bigint {
    size_t count;      // octets in buffer
    uint8_t *mpz_buf;  // buffer

    static constexpr int bigendian = 1;

public:

    bigint(mpz_t x) :
        count{(mpz_sizeinbase(x, 2) + 8-1) / 8},
        mpz_buf{(uint8_t *)mpz_export(nullptr, &count, bigendian, 1, 1, 0, x)} {
        if (mpz_buf == nullptr) {
            throw std::runtime_error{"could not allocate mpz buffer"};
        }
        // fprintf(stdout, "bytes in mpz_buf: %zu\n", count);
        // fprintf(stdout, "mpz_buf: %x%x%x%x\n", mpz_buf[0], mpz_buf[1], mpz_buf[2], mpz_buf[3]);
    }

    ~bigint() { free(mpz_buf); }

    datum get_datum() const { return { mpz_buf, mpz_buf + count }; }
};

uint8_t raw_hex[] = {
    0x30, 0x82, 0x02, 0x5c, 0x02, 0x01, 0x00, 0x02,
    0x81, 0x81, 0x00, 0xaa, 0x18, 0xab, 0xa4, 0x3b,
    0x50, 0xde, 0xef, 0x38, 0x59, 0x8f, 0xaf, 0x87,
    0xd2, 0xab, 0x63, 0x4e, 0x45, 0x71, 0xc1, 0x30,
    0xa9, 0xbc, 0xa7, 0xb8, 0x78, 0x26, 0x74, 0x14,
    0xfa, 0xab, 0x8b, 0x47, 0x1b, 0xd8, 0x96, 0x5f,
    0x5c, 0x9f, 0xc3, 0x81, 0x84, 0x85, 0xea, 0xf5,
    0x29, 0xc2, 0x62, 0x46, 0xf3, 0x05, 0x50, 0x64,
    0xa8, 0xde, 0x19, 0xc8, 0xc3, 0x38, 0xbe, 0x54,
    0x96, 0xcb, 0xae, 0xb0, 0x59, 0xdc, 0x0b, 0x35,
    0x81, 0x43, 0xb4, 0x4a, 0x35, 0x44, 0x9e, 0xb2,
    0x64, 0x11, 0x31, 0x21, 0xa4, 0x55, 0xbd, 0x7f,
    0xde, 0x3f, 0xac, 0x91, 0x9e, 0x94, 0xb5, 0x6f,
    0xb9, 0xbb, 0x4f, 0x65, 0x1c, 0xdb, 0x23, 0xea,
    0xd4, 0x39, 0xd6, 0xcd, 0x52, 0x3e, 0xb0, 0x81,
    0x91, 0xe7, 0x5b, 0x35, 0xfd, 0x13, 0xa7, 0x41,
    0x9b, 0x30, 0x90, 0xf2, 0x47, 0x87, 0xbd, 0x4f,
    0x4e, 0x19, 0x67, 0x02, 0x03, 0x01, 0x00, 0x01,
    0x02, 0x81, 0x80, 0x16, 0x28, 0xe4, 0xa3, 0x9e,
    0xbe, 0xa8, 0x6c, 0x8d, 0xf0, 0xcd, 0x11, 0x57,
    0x26, 0x91, 0x01, 0x7c, 0xfe, 0xfb, 0x14, 0xea,
    0x1c, 0x12, 0xe1, 0xde, 0xdc, 0x78, 0x56, 0x03,
    0x2d, 0xad, 0x0f, 0x96, 0x12, 0x00, 0xa3, 0x86,
    0x84, 0xf0, 0xa3, 0x6d, 0xca, 0x30, 0x10, 0x2e,
    0x24, 0x64, 0x98, 0x9d, 0x19, 0xa8, 0x05, 0x93,
    0x37, 0x94, 0xc7, 0xd3, 0x29, 0xeb, 0xc8, 0x90,
    0x08, 0x9d, 0x3c, 0x4c, 0x6f, 0x60, 0x27, 0x66,
    0xe5, 0xd6, 0x2a, 0xdd, 0x74, 0xe8, 0x2e, 0x49,
    0x0b, 0xbf, 0x92, 0xf6, 0xa4, 0x82, 0x15, 0x38,
    0x53, 0x03, 0x1b, 0xe2, 0x84, 0x4a, 0x70, 0x05,
    0x57, 0xb9, 0x76, 0x73, 0xe7, 0x27, 0xcd, 0x13,
    0x16, 0xd3, 0xe6, 0xfa, 0x7f, 0xc9, 0x91, 0xd4,
    0x22, 0x73, 0x66, 0xec, 0x55, 0x2c, 0xbe, 0x90,
    0xd3, 0x67, 0xef, 0x2e, 0x2e, 0x79, 0xfe, 0x66,
    0xd2, 0x63, 0x11, 0x02, 0x41, 0x00, 0xde, 0x03,
    0x0e, 0x9f, 0x88, 0x84, 0x17, 0x1a, 0xe9, 0x01,
    0x23, 0x87, 0x8c, 0x65, 0x9b, 0x78, 0x9e, 0xc7,
    0x32, 0xda, 0x8d, 0x76, 0x2b, 0x26, 0x27, 0x7a,
    0xbd, 0xd5, 0xa6, 0x87, 0x84, 0xf8, 0xda, 0x76,
    0xab, 0xe6, 0x77, 0xa6, 0xf0, 0x0c, 0x77, 0xf6,
    0x8d, 0xcd, 0x0f, 0xd6, 0xf5, 0x66, 0x88, 0xf8,
    0xd4, 0x5f, 0x73, 0x15, 0x09, 0xae, 0x67, 0xcf,
    0xc0, 0x81, 0xa6, 0xeb, 0x78, 0xa5, 0x02, 0x41,
    0x00, 0xc4, 0x22, 0xf9, 0x1d, 0x06, 0xf6, 0x6d,
    0x0a, 0xf8, 0x07, 0x2a, 0x2b, 0x70, 0xc5, 0xa6,
    0xfe, 0x11, 0x0f, 0xd8, 0xc6, 0x73, 0x44, 0xe5,
    0x7b, 0xdf, 0x21, 0x78, 0xd6, 0x13, 0xec, 0x44,
    0x2f, 0x66, 0xeb, 0xa2, 0xab, 0x85, 0xe3, 0xbd,
    0x1c, 0xf4, 0xc9, 0xba, 0x8d, 0xff, 0xf6, 0xce,
    0x69, 0xfa, 0xca, 0x86, 0xc4, 0xe9, 0x45, 0x2f,
    0x43, 0x43, 0xb7, 0x84, 0xa4, 0xa2, 0xc8, 0xe0,
    0x1b, 0x02, 0x40, 0x16, 0x49, 0x72, 0x47, 0x5b,
    0x99, 0xff, 0x03, 0xc9, 0x8e, 0x3e, 0xb5, 0xd5,
    0xc7, 0x41, 0x73, 0x3b, 0x65, 0x3d, 0xda, 0xa8,
    0xc6, 0xcb, 0x10, 0x1a, 0x78, 0x7c, 0xe4, 0x1c,
    0xc2, 0x8f, 0xfb, 0xb7, 0x5a, 0xa0, 0x69, 0x13,
    0x6b, 0xe3, 0xbf, 0x2c, 0xaf, 0xc8, 0x8e, 0x64,
    0x5f, 0xac, 0xe4, 0xed, 0x2d, 0x25, 0x8c, 0xab,
    0x6d, 0xda, 0x39, 0xf2, 0xdb, 0xed, 0x34, 0x56,
    0xc0, 0x5e, 0xad, 0x02, 0x41, 0x00, 0x91, 0x82,
    0xd4, 0xc8, 0x39, 0x3b, 0x27, 0x68, 0xe4, 0xdc,
    0x03, 0xe8, 0x18, 0x91, 0x3a, 0xb3, 0xf1, 0x1a,
    0x8d, 0x9b, 0xa5, 0x36, 0xee, 0xfd, 0xf8, 0x6b,
    0x4f, 0xc7, 0x9b, 0x1e, 0x44, 0xf3, 0xd9, 0xea,
    0x65, 0x53, 0xd5, 0x50, 0x41, 0x24, 0x33, 0x63,
    0x5a, 0x19, 0x31, 0x55, 0xfc, 0x8b, 0x59, 0xb9,
    0x59, 0x44, 0xcb, 0x3f, 0x3d, 0xb2, 0x2c, 0x92,
    0x01, 0x41, 0x57, 0x57, 0xaa, 0x13, 0x02, 0x40,
    0x11, 0xa8, 0x8a, 0xe4, 0xa8, 0x4a, 0x36, 0x9f,
    0x52, 0x15, 0x7b, 0x8b, 0x57, 0x04, 0x1a, 0x96,
    0xfc, 0xf2, 0x1e, 0x4d, 0x05, 0x86, 0x73, 0x59,
    0x71, 0x99, 0xdf, 0xbb, 0x09, 0xe5, 0x0b, 0x16,
    0xfa, 0xc2, 0x72, 0xa0, 0xd7, 0x5e, 0xdf, 0x11,
    0xfc, 0xbd, 0xd5, 0xe1, 0xcd, 0x4e, 0xde, 0x4f,
    0xcd, 0x83, 0xe9, 0x7f, 0xec, 0x73, 0x0f, 0x51,
    0x67, 0x3f, 0xbf, 0xea, 0xb0, 0x89, 0xe2, 0x9d
};


bool test_decoding() {

    uint8_t file[] = {
        0x30, 0x82, 0x04, 0xbd, 0x02, 0x01, 0x00, 0x30, 0x0d, 0x06, 0x09, 0x2a,
        0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x04, 0x82,
        0x04, 0xa7, 0x30, 0x82, 0x04, 0xa3, 0x02, 0x01, 0x00, 0x02, 0x82, 0x01,
        0x01, 0x00, 0xf2, 0xf9, 0x32, 0x99, 0xe0, 0x98, 0xcd, 0x62, 0x57, 0x85,
        0xa4, 0x4b, 0x4e, 0x20, 0x43, 0x73, 0x56, 0x33, 0xaa, 0x42, 0x42, 0x53,
        0xee, 0xce, 0x01, 0x00, 0xa8, 0x5e, 0x01, 0x63, 0x84, 0x1f, 0x0f, 0x2e,
        0x8b, 0x85, 0xb4, 0x5e, 0xc4, 0x87, 0x69, 0xed, 0x76, 0xcd, 0xca, 0xa3,
        0x5f, 0x37, 0x50, 0x0d, 0xd9, 0x36, 0x09, 0x9b, 0x79, 0xb4, 0x69, 0x9d,
        0x7c, 0x4a, 0x75, 0x48, 0xfa, 0xf4, 0x9b, 0xb1, 0xb9, 0x94, 0x95, 0xeb,
        0xbe, 0x8a, 0xca, 0x37, 0x61, 0x7e, 0x7c, 0xf6, 0x7a, 0xb8, 0xb6, 0x3a,
        0xe1, 0x50, 0x06, 0x98, 0xe1, 0xf0, 0x55, 0x09, 0xa4, 0x6c, 0x0f, 0x91,
        0xf0, 0xea, 0x3a, 0xaa, 0x02, 0x92, 0x83, 0x6c, 0x0c, 0xad, 0x5a, 0xb1,
        0x66, 0x25, 0xe8, 0xe2, 0x34, 0xaf, 0x43, 0xf7, 0x8c, 0x5f, 0xf5, 0x8d,
        0x46, 0x29, 0x4b, 0xbe, 0x38, 0xd7, 0x13, 0xb0, 0x6c, 0xe6, 0x53, 0xeb,
        0xf2, 0xc5, 0x69, 0x24, 0x87, 0xe7, 0xa0, 0xe0, 0x27, 0x41, 0x8d, 0x59,
        0x9a, 0xf4, 0xeb, 0xa5, 0x6a, 0x2c, 0x7f, 0x7f, 0xd4, 0xf3, 0x91, 0x6c,
        0xb1, 0xb7, 0x01, 0xd2, 0xf3, 0xb0, 0xf2, 0x07, 0x5a, 0x58, 0x9e, 0x4a,
        0x5f, 0x31, 0x2d, 0x32, 0x48, 0x7c, 0xb6, 0x66, 0xaf, 0x4f, 0xb3, 0x80,
        0xe5, 0x92, 0x70, 0xcc, 0xd9, 0x5c, 0xfb, 0xe8, 0x6d, 0xcf, 0xa6, 0x6e,
        0x53, 0xc4, 0x12, 0xf7, 0xcd, 0x91, 0x8c, 0x8f, 0xb9, 0xc4, 0x5b, 0x0e,
        0x1e, 0x6d, 0x8a, 0x06, 0xd4, 0x9c, 0x99, 0x56, 0x9e, 0x6f, 0x13, 0x25,
        0xcd, 0xf7, 0x59, 0x27, 0x91, 0x50, 0x08, 0x82, 0x6d, 0xd6, 0x56, 0x82,
        0x32, 0x44, 0x2d, 0x7e, 0xf3, 0x16, 0xbc, 0x32, 0xa1, 0xec, 0x72, 0x8a,
        0x22, 0xed, 0x11, 0x86, 0xe3, 0xc7, 0xc6, 0xf5, 0xd4, 0xa4, 0x49, 0x2b,
        0x16, 0x6b, 0x4a, 0x9c, 0x38, 0x63, 0x02, 0x03, 0x01, 0x00, 0x01, 0x02,
        0x82, 0x01, 0x01, 0x00, 0xce, 0x1e, 0x30, 0x9a, 0xf1, 0x39, 0x2f, 0x22,
        0x79, 0xf4, 0xd9, 0x47, 0x38, 0xe3, 0x8d, 0xd4, 0xce, 0x0f, 0xce, 0x23,
        0x9f, 0x78, 0xec, 0x60, 0xbd, 0xe0, 0xfc, 0xf3, 0xa2, 0x61, 0xf5, 0xb7,
        0x13, 0x7d, 0xfc, 0xc6, 0x54, 0x19, 0x00, 0xc7, 0x8f, 0x48, 0xef, 0x3b,
        0xec, 0xe7, 0x62, 0xe2, 0xdd, 0x7a, 0xa2, 0x05, 0x81, 0x68, 0xef, 0x79,
        0xe9, 0x0e, 0xbc, 0x5d, 0xbd, 0xd9, 0x47, 0x6b, 0x32, 0x99, 0x36, 0x41,
        0xa2, 0x5c, 0xf6, 0xab, 0x6e, 0x98, 0x44, 0x90, 0xb5, 0x19, 0xb3, 0x49,
        0xf6, 0xed, 0x44, 0x2e, 0x4b, 0x2a, 0x6e, 0xa1, 0x1e, 0xc2, 0xab, 0x45,
        0x30, 0x80, 0x31, 0xcb, 0xc2, 0x30, 0x6f, 0x36, 0x33, 0x5e, 0xf9, 0xf2,
        0x25, 0xb9, 0xd0, 0x59, 0xe0, 0x91, 0xe4, 0xf7, 0xb9, 0xc4, 0xca, 0xc4,
        0xac, 0xde, 0x47, 0xe2, 0xc8, 0x6a, 0x7a, 0x75, 0x9a, 0x32, 0x54, 0x6d,
        0xf9, 0x16, 0x30, 0x2b, 0x9f, 0xb6, 0xff, 0xce, 0x53, 0x90, 0x25, 0xc1,
        0xb8, 0x1b, 0xf8, 0xfd, 0xb2, 0x0f, 0x9a, 0x32, 0x1a, 0x35, 0x2c, 0xf1,
        0xb2, 0x82, 0xd0, 0x82, 0xaa, 0xcb, 0xf4, 0x37, 0x43, 0x43, 0x4a, 0x3c,
        0x24, 0x95, 0x34, 0xd5, 0xe1, 0xf0, 0x97, 0x4b, 0xe1, 0xd6, 0x6e, 0x23,
        0x3e, 0x93, 0x2c, 0x3f, 0x4b, 0x06, 0x08, 0x14, 0xad, 0x5c, 0x07, 0x68,
        0x6a, 0x7f, 0xee, 0xa2, 0xe7, 0x1f, 0xd8, 0x92, 0x13, 0xec, 0x34, 0x98,
        0x76, 0x6e, 0xcd, 0x9e, 0x52, 0x28, 0x9e, 0x27, 0x0f, 0xcf, 0x14, 0x9c,
        0x8a, 0x1b, 0x0e, 0x81, 0x93, 0x6a, 0x9b, 0x6f, 0x1b, 0xec, 0x2c, 0x68,
        0x9c, 0x03, 0xbf, 0xe0, 0xf9, 0x0c, 0x45, 0x10, 0xbb, 0xc4, 0x34, 0x72,
        0x2d, 0x61, 0x4f, 0xe6, 0x75, 0x24, 0xd4, 0x9e, 0xd1, 0xc2, 0x58, 0x97,
        0x49, 0x6a, 0x2a, 0x58, 0x1b, 0x5f, 0x04, 0xc1, 0x02, 0x81, 0x81, 0x00,
        0xfd, 0x9d, 0x96, 0x1f, 0x92, 0xea, 0xca, 0xad, 0x8d, 0xfd, 0xcd, 0x32,
        0x4d, 0x7c, 0x71, 0x40, 0x89, 0xad, 0xfe, 0xcd, 0xd9, 0x08, 0x16, 0x73,
        0xdb, 0x19, 0x5f, 0x6c, 0x23, 0x9b, 0x53, 0xa5, 0x37, 0xf9, 0x35, 0x10,
        0xc4, 0x86, 0x60, 0x74, 0x7e, 0x65, 0xa9, 0x65, 0xcb, 0xfa, 0xc3, 0xeb,
        0xbd, 0x13, 0x76, 0xc3, 0x17, 0x65, 0x4d, 0x96, 0x76, 0xd1, 0xbf, 0x39,
        0x68, 0x3d, 0x57, 0x9b, 0x46, 0x10, 0xbd, 0x90, 0x85, 0x52, 0xfc, 0xa3,
        0x00, 0xad, 0x99, 0x3a, 0x85, 0xc6, 0xa3, 0x14, 0x5c, 0xff, 0x60, 0x0c,
        0xb1, 0x7a, 0x13, 0x1f, 0xe1, 0x7d, 0xbb, 0xb7, 0x5a, 0x33, 0x5c, 0xa8,
        0x45, 0x25, 0xd3, 0xe8, 0x4d, 0x55, 0x6c, 0xe0, 0xa4, 0xfd, 0x11, 0x30,
        0x4b, 0xf9, 0xa5, 0xca, 0x9f, 0x63, 0x36, 0xf3, 0xa2, 0x7f, 0xf8, 0x54,
        0xa1, 0xb7, 0x8a, 0x43, 0x58, 0x6c, 0xe5, 0x2b, 0x02, 0x81, 0x81, 0x00,
        0xf5, 0x41, 0xff, 0x4b, 0x45, 0xec, 0x16, 0xae, 0x0a, 0x45, 0x74, 0xc6,
        0xbe, 0xcc, 0x0c, 0x06, 0x4b, 0x65, 0xd1, 0xbb, 0x7f, 0x1c, 0x4c, 0xbb,
        0x80, 0x60, 0x04, 0x30, 0xd8, 0x83, 0x8d, 0x18, 0xc0, 0x49, 0x93, 0x55,
        0x98, 0xab, 0x08, 0x06, 0x32, 0x71, 0xaa, 0x2c, 0xe2, 0x45, 0x17, 0xdd,
        0x19, 0x30, 0xcd, 0xe4, 0xcf, 0x6c, 0xb9, 0xca, 0x82, 0x94, 0x96, 0x83,
        0x01, 0x50, 0xf9, 0x0d, 0x4f, 0xcf, 0x2c, 0xcd, 0x45, 0x0e, 0xd6, 0x57,
        0x9e, 0x61, 0xc1, 0x0d, 0xf5, 0x9c, 0xe4, 0x8f, 0xf6, 0x65, 0x42, 0x20,
        0x01, 0xbf, 0xa7, 0xec, 0x40, 0xcf, 0xdf, 0xc1, 0x6c, 0xff, 0xf5, 0x2e,
        0x1a, 0x4a, 0xcc, 0x38, 0xf3, 0x20, 0xac, 0x71, 0x94, 0x31, 0x87, 0xdd,
        0x59, 0xd8, 0x18, 0x2a, 0x36, 0xb4, 0xb1, 0xe9, 0xf0, 0x67, 0x72, 0x49,
        0x45, 0xa4, 0x56, 0xe5, 0xbd, 0xe1, 0x4d, 0xa9, 0x02, 0x81, 0x80, 0x76,
        0x55, 0xb8, 0x3d, 0x65, 0x3c, 0xbe, 0x72, 0xfa, 0x84, 0xc8, 0xe0, 0xc6,
        0xbc, 0xe0, 0xce, 0xff, 0x2e, 0xbb, 0x6c, 0x6a, 0xee, 0xd6, 0x23, 0x1a,
        0xc1, 0x1d, 0x00, 0x05, 0x21, 0x2d, 0x87, 0x32, 0xb5, 0xc9, 0xe7, 0xd7,
        0xfa, 0xe7, 0x38, 0x93, 0xdd, 0x75, 0x8b, 0xf5, 0x00, 0x3d, 0xb8, 0x5a,
        0x11, 0xa1, 0xe1, 0x67, 0xa2, 0x31, 0xf0, 0x99, 0xe2, 0x46, 0x3a, 0x50,
        0x04, 0x07, 0x43, 0x81, 0x0e, 0xc0, 0x94, 0x95, 0x50, 0xe2, 0x66, 0x60,
        0x23, 0xa0, 0x12, 0x69, 0x67, 0x04, 0xa2, 0xb4, 0xbd, 0xc7, 0xa0, 0x44,
        0x93, 0x34, 0x27, 0x34, 0xfc, 0x88, 0xc1, 0x05, 0x8a, 0x5f, 0x9a, 0x78,
        0x21, 0x2d, 0x5d, 0xff, 0xef, 0x73, 0x0c, 0xe2, 0x8e, 0xde, 0x1d, 0x4d,
        0xe5, 0xdf, 0x50, 0xca, 0xcb, 0xed, 0x51, 0x02, 0xaa, 0x79, 0x41, 0x6b,
        0xef, 0x8a, 0xc8, 0xdf, 0x92, 0x77, 0xdf, 0x02, 0x81, 0x80, 0x37, 0x6e,
        0x3f, 0x20, 0xe8, 0x20, 0xbf, 0xcf, 0x7e, 0x0a, 0xcc, 0xa5, 0xce, 0xa1,
        0x97, 0x66, 0x24, 0xcc, 0x52, 0x66, 0xaa, 0x07, 0xdf, 0x5f, 0xd1, 0x57,
        0xe2, 0x1a, 0x98, 0x14, 0xc3, 0x63, 0x00, 0xb2, 0xa0, 0x56, 0x0c, 0x37,
        0x3b, 0x8d, 0x0b, 0x01, 0x9d, 0x90, 0x9f, 0x63, 0x36, 0x4d, 0x86, 0x4f,
        0xfd, 0x78, 0xe5, 0x58, 0x91, 0x75, 0x2f, 0xa6, 0x1d, 0x8e, 0x66, 0x51,
        0xc2, 0xb8, 0x3b, 0x7d, 0x7b, 0x86, 0xb9, 0x40, 0xed, 0x38, 0xc8, 0x57,
        0x17, 0xa6, 0xec, 0x08, 0x15, 0xb0, 0x63, 0xe3, 0xe6, 0xda, 0x0d, 0x0b,
        0x20, 0x0c, 0xc9, 0x69, 0x32, 0x0d, 0x29, 0x71, 0x80, 0x1c, 0x77, 0x5c,
        0xc8, 0x63, 0x66, 0xaf, 0xcf, 0xc9, 0xab, 0xd0, 0xb6, 0x00, 0x55, 0x39,
        0xfd, 0xdc, 0x2c, 0x99, 0x12, 0x4c, 0xe9, 0x44, 0xb8, 0x13, 0xcf, 0x65,
        0xa1, 0x2e, 0x33, 0x88, 0x24, 0x61, 0x02, 0x81, 0x80, 0x11, 0xa6, 0x9a,
        0x5c, 0x71, 0xe1, 0x7e, 0x9e, 0x71, 0x87, 0x0d, 0x34, 0x1d, 0x2d, 0x8f,
        0x66, 0x76, 0xc4, 0x47, 0x96, 0xaf, 0xc2, 0xae, 0x06, 0xd5, 0xd4, 0xf3,
        0xf2, 0x79, 0x6b, 0x42, 0x71, 0xe9, 0x0f, 0x98, 0x99, 0x19, 0x15, 0x2d,
        0x42, 0x7e, 0x92, 0x32, 0xd6, 0x46, 0xa0, 0x7a, 0x36, 0xa0, 0x9d, 0x02,
        0x6b, 0x3a, 0x2f, 0xb2, 0xfc, 0x61, 0xa3, 0xd0, 0xee, 0xac, 0xd7, 0xd0,
        0x97, 0x87, 0x30, 0x9d, 0x4b, 0x7c, 0x8b, 0xa2, 0x81, 0x5e, 0x32, 0x77,
        0x71, 0xe4, 0x52, 0x26, 0x20, 0x8b, 0xba, 0xd3, 0x49, 0xa1, 0xdc, 0x32,
        0xef, 0xa7, 0x9f, 0x6c, 0x37, 0x46, 0x83, 0x2c, 0x69, 0xc4, 0x21, 0x16,
        0x35, 0x09, 0x5b, 0x11, 0xf9, 0x7d, 0x79, 0x44, 0xb0, 0x7c, 0x3d, 0x5a,
        0x50, 0x10, 0x41, 0x2f, 0xfc, 0x09, 0xe4, 0x61, 0x78, 0xca, 0x6c, 0x8e,
        0x3a, 0xbf, 0x3d, 0x8f, 0xb1
    };
    datum f{file, file + sizeof(file)};
    fprintf(stdout, "original: "); f.fprint_hex(stdout); fputc('\n', stdout);

    private_key_info pkinfo{f};

    data_buffer<4096> dbuf;
    pkinfo.write(dbuf);
    datum result = dbuf.contents();
    fprintf(stdout, "result:   "); result.fprint_hex(stdout); fputc('\n', stdout);

    // create second pkinfo from public key, then compare to original
    //
    rsa_private_key rsa_priv = pkinfo.get_rsa_private_key();
    private_key_info pkinfo2{rsa_priv};
    data_buffer<4096> dbuf2;
    pkinfo2.write(dbuf2);
    datum result2 = dbuf2.contents();
    fprintf(stdout, "result2:  "); result2.fprint_hex(stdout); fputc('\n', stdout);

    if (dbuf.readable_length() == dbuf2.readable_length() &&
        memcmp(dbuf.buffer, dbuf2.buffer, dbuf.readable_length()) == 0) {
        fprintf(stdout, "success: result == result2\n");
    } else {
        fprintf(stdout, "failure: result != result2\n          ");
        //size_t min_len = std::min(dbuf.readable_length(), dbuf2.readable_length());
        for (ssize_t i=0; i<std::min(dbuf.readable_length(), dbuf2.readable_length()); i++) {
            if (dbuf.buffer[i] == dbuf2.buffer[i]) {
                fprintf(stdout, "%02x", dbuf.buffer[i]);
            } else {
                fprintf(stdout, "**");
            }
        }
        fputc('\n', stdout);
    }

    return 0;
}

void read_file(const char *filename) {

}

int main(int, char *[]) {

    assert(private_key_info::unit_test());
    return 0;

    printf("raw:    ");
    for (auto &c : raw_hex) {
        printf("%02x", c);
    }
    putc('\n', stdout);
    datum raw{raw_hex, raw_hex + sizeof(raw_hex)};
    rsa_private_key priv{raw};

    // output_buffer<4096> buf;
    // json_object o{&buf};
    // priv.write_json(o);
    // o.close();
    // buf.write_line(stdout);

    data_buffer<1024> dbuf;
    priv.write(dbuf);
    datum result = dbuf.contents();
    fprintf(stdout, "result: "); result.fprint_hex(stdout); fputc('\n', stdout);

    std::array<uint8_t, 128> mod{
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    std::array<uint8_t, 3> priv_exp{  0xab, 0xcd, 0xef };
    rsa_private_key priv2{
        datum{mod},
        datum{priv_exp},
        datum{priv_exp},
        datum{priv_exp}, // note: private key not mathematically valid
        datum{priv_exp},
        datum{priv_exp},
        datum{priv_exp}
    };

    data_buffer<1024> dbuf2;
    fprintf(stdout, "dbuf2 length:  %lu\n", dbuf2.contents().length());
    priv2.write(dbuf2);
    datum result2 = dbuf2.contents();
    fprintf(stdout, "dbuf2:  {data: %p, data_end: %p}\n", dbuf2.data, dbuf2.data_end);
    fprintf(stdout, "result: {data: %p, data_end: %p}\n", result2.data, result2.data_end);
    fprintf(stdout, "result length: %lu\n", result2.length());
    fprintf(stdout, "result: "); result2.fprint_hex(stdout); fputc('\n', stdout);

    write_pem(fopen("pkcs8.pem", "w+"), result2.data, result2.length());

    output_buffer<4096> buf;
    json_object o{&buf};
    priv2.write_json(o);
    o.close();
    buf.write_line(stdout);

    mpz_t x;
    mpz_init_set_ui(x, 0xaabbccdd);

    bigint bigint_x{x};

    //    uint8_t mpz_buf[32];
    //    size_t count = sizeof(mpz_buf)/sizeof(uint64_t);
    // static constexpr int bigendian = 1;
    // size_t count = (mpz_sizeinbase(x, 2) + 8-1) / 8;
    // uint8_t *mpz_buf = (uint8_t *)mpz_export(nullptr, &count, bigendian, 1, 1, 0, x);

    // fprintf(stdout, "bytes in mpz_buf: %zu\n", count);
    // fprintf(stdout, "mpz_buf: %x%x%x%x\n", mpz_buf[0], mpz_buf[1], mpz_buf[2], mpz_buf[3]);
    // free(mpz_buf);

    return 0;
}
