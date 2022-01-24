/*
 * quic.h
 *
 * Copyright (c) 2020 Cisco Systems, Inc. All rights reserved.
 * License at https://github.com/cisco/mercury/blob/master/LICENSE
 */

/**
 * \file quic.h
 *
 * \brief interface file for QUIC code
 */
#ifndef QUIC_H
#define QUIC_H

#include <string>
#include <unordered_map>
#include <variant>
#include <arpa/inet.h>
#include <openssl/aes.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include "tls.h"
#include "json_object.h"
#include "util_obj.h"
#include "match.h"
#include "crypto_engine.h"

/*
 * QUIC header format (from draft-ietf-quic-transport-32):
 *
 *    Long Header Packet {
 *       Header Form (1) = 1,
 *       Fixed Bit (1) = 1,
 *       Long Packet Type (2),
 *       Type-Specific Bits (4),
 *       Version (32),
 *       Destination Connection ID Length (8),
 *       Destination Connection ID (0..160),
 *       Source Connection ID Length (8),
 *       Source Connection ID (0..160),
 *    }
 *
 *    Short Header Packet {
 *       Header Form (1) = 0,
 *       Fixed Bit (1) = 1,
 *       Spin Bit (1),
 *       Reserved Bits (2),
 *       Key Phase (1),
 *       Packet Number Length (2),
 *       Destination Connection ID (0..160),
 *       Packet Number (8..32),
 *       Packet Payload (..),
 *    }
 *
 */

struct uint8_bitfield {
    uint8_t value;

    uint8_bitfield(uint8_t x) : value{x} {}

    void fingerprint(struct buffer_stream &b) {
        for (uint8_t x = 0x80; x > 0; x=x>>1) {
            if (x & value) {
                b.write_char('1');
            } else {
                b.write_char('0');
            }
        }
    }
};

// class variable_length_integer implements the QUIC variable-length
// integer encoding (following RFC9000, Section 16).  If there is a
// parse error, i.e. the datum being parsed is too short, then the datum
// reference passed to the constructor will be set to NULL state.  The
// value of the variable length integer is returned by the member function
// value().
//
//          +======+========+=============+=======================+
//          | 2MSB | Length | Usable Bits | Range                 |
//          +======+========+=============+=======================+
//          | 00   | 1      | 6           | 0-63                  |
//          +------+--------+-------------+-----------------------+
//          | 01   | 2      | 14          | 0-16383               |
//          +------+--------+-------------+-----------------------+
//          | 10   | 4      | 30          | 0-1073741823          |
//          +------+--------+-------------+-----------------------+
//          | 11   | 8      | 62          | 0-4611686018427387903 |
//          +------+--------+-------------+-----------------------+
//
class variable_length_integer {
    uint64_t value_;

public:

    variable_length_integer(datum &d) : value_{0} {
        uint8_t b;
        d.read_uint8(&b);
        int len=0;
        switch (b & 0xc0) {
        case 0xc0:
            len = 8;
            break;
        case 0x80:
            len = 4;
            break;
        case 0x40:
            len = 2;
            break;
        case 0x00:
            len = 1;
        }
        value_ = (b & 0x3f);
        for (int i=1; i<len; i++) {
            value_ *= 256;
            d.read_uint8(&b);
            value_ += b;
        }
    }

    uint64_t value() const { return value_; }

};

class variable_length_integer_datum : public datum {

public:

    variable_length_integer_datum(datum &d) {
        uint8_t b;
        d.lookahead_uint8(&b);
        int len=0;
        switch (b & 0xc0) {
        case 0xc0:
            len = 8;
            break;
        case 0x80:
            len = 4;
            break;
        case 0x40:
            len = 2;
            break;
        case 0x00:
            len = 1;
        }
        datum::parse(d, len);
    }

    void write(buffer_stream &b) const {
        b.raw_as_hex(data, length());
    }

    bool is_grease() const {
        datum tmp = *this;               // copy to avoid changing *this
        variable_length_integer v{tmp};
        return v.value() % 31 == 27;
    }
};


// quic_transport_parameters are carried in a TLS extension; see
// https://datatracker.ietf.org/doc/html/rfc9000#section-18 and
// https://www.iana.org/assignments/quic/quic.xhtml#quic-transport
//
//   Transport Parameter {
//     Transport Parameter ID (i),
//     Transport Parameter Length (i),
//     Transport Parameter Value (..),
//   }
//
class quic_transport_parameter {
    variable_length_integer_datum _id;
    variable_length_integer _length;
    datum _value;

public:

    quic_transport_parameter(datum &d) : _id{d}, _length{d}, _value{d, _length.value()} { }

    bool is_not_empty() const {
        return _value.is_not_null(); // note: zero-length value is possible
    }

    void write_id(buffer_stream &b) const {
        if (!_id.is_grease()) {
            _id.write(b);
        } else {
            // write out the smallest GREASE value (0x1b == 27)
            b.write_char('1');
            b.write_char('b');
        }
    }

    variable_length_integer_datum get_id() const { return _id; }

};

// quic frames are defined by a set of classes and the std::variant
// quic_frame, defined below
//

// PADDING Frame {
//   Type (i) = 0x00,
// }
//
// PING Frame {
//   Type (i) = 0x01,
// }
//
//
// ACK Range {
//   Gap (i),
//   ACK Range Length (i),
// }
//
class ack_range {
    variable_length_integer gap;
    variable_length_integer length;
public:

    ack_range(datum &d) : gap{d}, length{d} { }
};

// ACK Frame {
//   Type (i) = 0x02..0x03,
//   Largest Acknowledged (i),
//   ACK Delay (i),
//   ACK Range Count (i),
//   First ACK Range (i),
//   ACK Range (..) ...,
//   [ECN Counts (..)],
// }
//
class ack {
    variable_length_integer largest_acked;
    variable_length_integer ack_delay;
    variable_length_integer ack_range_count;
    variable_length_integer first_ack_range;
    bool valid;

public:
    ack(datum &d) : largest_acked{d}, ack_delay{d}, ack_range_count{d}, first_ack_range{d}, valid{false} {
        for (unsigned i=0; i<ack_range_count.value(); i++) {
            ack_range range{d};
        }
        if (d.is_null()) {
            return;
        }
        valid = true;
    }

    bool is_valid() const { return valid; }

    void write_json(json_object &o) {
        if (is_valid()) {
            json_object a{o, "ack"};
            a.print_key_uint("largest_acked", largest_acked.value());
            a.print_key_uint("ack_delay", ack_delay.value());
            a.print_key_uint("ack_range_count", ack_range_count.value());
            a.print_key_uint("first_ack_range", first_ack_range.value());
            a.close();
        }
    }

	void write(FILE *f) {
    	if (is_valid()) {
        	fprintf(f, "ack.largest_acked: %lu\n", largest_acked.value());
        	fprintf(f, "ack.ack_delay: %lu\n", ack_delay.value());
        	fprintf(f, "ack.ack_range_count: %lu\n", ack_range_count.value());
        	fprintf(f, "ack.first_ack_range: %lu\n", first_ack_range.value());
        } else {
        	fprintf(f, "ack.not valid\n");
        }
    }

};

//
// ECN Counts {
//   ECT0 Count (i),
//   ECT1 Count (i),
//   ECN-CE Count (i),
// }
//
// RESET_STREAM Frame {
//   Type (i) = 0x04,
//   Stream ID (i),
//   Application Protocol Error Code (i),
//   Final Size (i),
// }
//
// STOP_SENDING Frame {
//   Type (i) = 0x05,
//   Stream ID (i),
//   Application Protocol Error Code (i),
// }
//
// CRYPTO Frame {
//   Type (i) = 0x06,
//   Offset (i),
//   Length (i),
//   Crypto Data (..),
// }
//
class crypto {
    variable_length_integer _offset;
    variable_length_integer _length;
    datum _data;

public:
    crypto(datum &p) : _offset{p}, _length{p}, _data{p, _length.value()} {    }

    bool is_valid() const { return _data.is_not_empty(); }

    datum &data() { return _data; } // note: function is not const

    uint64_t offset() const
    {
        return _offset.value();
    }

    uint64_t length() const
    {
        return _length.value();
    }

    void write(FILE *f) {
        if (is_valid()) {
            fprintf(f, "crypto.offset: %lu\n", _offset.value());
            fprintf(f, "crypto.length: %lu\n", _length.value());
        } else {
            fprintf(f, "crypto.not valid\n");
        }
    }
};

// NEW_TOKEN Frame {
//   Type (i) = 0x07,
//   Token Length (i),
//   Token (..),
// }
//
// STREAM Frame {
//   Type (i) = 0x08..0x0f,
//   Stream ID (i),
//   [Offset (i)],
//   [Length (i)],
//   Stream Data (..),
// }
//
// MAX_DATA Frame {
//   Type (i) = 0x10,
//   Maximum Data (i),
// }
//
// MAX_STREAM_DATA Frame {
//   Type (i) = 0x11,
//   Stream ID (i),
//   Maximum Stream Data (i),
// }
//
// MAX_STREAMS Frame {
//   Type (i) = 0x12..0x13,
//   Maximum Streams (i),
// }
//
// DATA_BLOCKED Frame {
//   Type (i) = 0x14,
//   Maximum Data (i),
// }
//
// STREAM_DATA_BLOCKED Frame {
//   Type (i) = 0x15,
//   Stream ID (i),
//   Maximum Stream Data (i),
// }
//
// STREAMS_BLOCKED Frame {
//   Type (i) = 0x16..0x17,
//   Maximum Streams (i),
// }
//
// NEW_CONNECTION_ID Frame {
//   Type (i) = 0x18,
//   Sequence Number (i),
//   Retire Prior To (i),
//   Length (8),
//   Connection ID (8..160),
//   Stateless Reset Token (128),
// }
//
// RETIRE_CONNECTION_ID Frame {
//   Type (i) = 0x19,
//   Sequence Number (i),
// }
//
// PATH_CHALLENGE Frame {
//   Type (i) = 0x1a,
//   Data (64),
// }
//
// PATH_RESPONSE Frame {
//   Type (i) = 0x1b,
//   Data (64),
// }
//
// CONNECTION_CLOSE Frame {
//   Type (i) = 0x1c..0x1d,
//   Error Code (i),
//   [Frame Type (i)],
//   Reason Phrase Length (i),
//   Reason Phrase (..),
// }
//
class connection_close {
    variable_length_integer error_code;
    variable_length_integer frame_type;
    variable_length_integer reason_phrase_length;
    datum reason_phrase;

public:
    connection_close(datum &p) : error_code{p}, frame_type{p}, reason_phrase_length{p}, reason_phrase{p, reason_phrase_length.value()} { }

    bool is_valid() const { return reason_phrase.is_not_empty(); }

	void write_json(json_object &o) {
        if (is_valid()) {
            json_object cc{o, "connection_close"};
            cc.print_key_uint("error_code", error_code.value());
            cc.print_key_uint("frame_type", frame_type.value());
            cc.print_key_json_string("reason_phrase", reason_phrase);
            cc.close();
        }
    }

	void write(FILE *f) {
    	if (is_valid()) {
        	fprintf(f, "connection_close.error_code: %lu\n", error_code.value());
        	fprintf(f, "connection_close.frame_type: %lu\n", frame_type.value());
        	fprintf(f, "connection_close.reason_phrase_length: %lu\n", reason_phrase_length.value());
        	fprintf(f, "connection_close.reason_phrase: %s\n", reason_phrase.get_string().c_str());
        } else {
        	fprintf(f, "connection_close.not valid\n");
        }
    }
};


// HANDSHAKE_DONE Frame {
//   Type (i) = 0x1e,
// }



//   Initial Packet {
//     Header Form (1) = 1,
//     Fixed Bit (1) = 1,
//     Long Packet Type (2) = 0,
//     Reserved Bits (2),
//     Packet Number Length (2),
//     Version (32),
//     Destination Connection ID Length (8),
//     Destination Connection ID (0..160),
//     Source Connection ID Length (8),
//     Source Connection ID (0..160),
//     Token Length (i),
//     Token (..),
//     Length (i),
//     Packet Number (8..32),
//     Packet Payload (8..),
//   }
//
struct quic_initial_packet {
    uint8_t connection_info;
    struct datum version;
    struct datum dcid;
    struct datum scid;
    struct datum token;
    struct datum payload;
    bool valid;
    bool gquic;
    const uint8_t *aad_start = nullptr;
    const uint8_t *aad_end = nullptr;

    quic_initial_packet(struct datum &d) : connection_info{0}, dcid{}, scid{}, token{}, payload{}, valid{false}, gquic{false} {
        parse(d);
    }

    void parse(struct datum &d) {

        // additional authenticated data (aad) is used in authenticated decryption
        //
        aad_start = d.data;

        if (d.length() < min_len_pdu) {
            return;  // packet too short to be valid
        }

        // connection information octet for initial packets:
        //
        // Header Form        (1)        1
        // Fixed Bit          (1)        ?
        // Long Packet Type   (2)        00
        // Type-Specific Bits (4)        ??
        //
        uint8_t conn_info_mask  = 0b10110000;
        uint8_t conn_info_value = 0b10000000;
        d.read_uint8(&connection_info);
        if ((connection_info & conn_info_mask) != conn_info_value) {
            return;
        }

        version.parse(d, 4);

        // don't process non-standard versions
        //
        uint64_t v = 0;
        version.lookahead_uint(4, &v);
        switch(v) {
        case 4278190102:   // draft-22
        case 4278190103:   // draft-23
        case 4278190104:   // draft-24
        case 4278190105:   // draft-25
        case 4278190106:   // draft-26
        case 4278190107:   // draft-27
        case 4278190108:   // draft-28
        case 4278190109:   // draft-29
        case 4278190110:   // draft-30
        case 4278190111:   // draft-31
        case 4278190112:   // draft-32
        case 4278190113:   // draft-33
        case 4278190114:   // draft-34
        case 1:            // version-1
            break;
        case 0x51303433:   // Google QUIC Q043
        case 0x51303436:   // Google QUIC Q046
        case 0x51303530:   // Google QUIC Q050
            gquic=true;    // TODO: report gquic, but don't decrypt it
            break;
        default:
            return; // TODO: parse and report non-standard quic
        }

        uint8_t dcid_length;
        d.read_uint8(&dcid_length);
        if (dcid_length > 20) {
            return;  // dcid too long
        }
        dcid.parse(d, dcid_length);

        uint8_t scid_length;
        d.read_uint8(&scid_length);
        if (scid_length > 20) {
            return;  // scid too long
        }
        scid.parse(d, scid_length);

        variable_length_integer token_length{d};
        token.parse(d, token_length.value());

        variable_length_integer length{d}; // length of packet number and packet payload
        //fprintf(stderr, "length: %08lu\td.length(): %08zu\tversion: %08lx\n", length.value(), d.length(), v);
        if (d.length() < (ssize_t)length.value() || length.value() < min_len_pn_and_payload) {
            //fprintf(stderr, "invalid\n");
            return;
        }

        // remember where aad ends
        //
        aad_end = d.data;

        payload.parse(d, length.value());

        if ((payload.is_not_empty() == false) || (dcid.is_not_empty() == false)) {
            //fprintf(stderr, "invalid\n");
            return;  // invalid or incomplete packet
        }
        // fprintf(stderr, "VALID\n");
        valid = true;
    }

	static constexpr size_t min_len_pn_and_payload = 64;  // TODO: determine best length bound
	static constexpr ssize_t min_len_pdu = 1200;          // TODO: determine best length bound

    bool is_not_empty() const {
        return valid;
    }

    void write_json(struct json_object &json_quic, bool =false) const {
        if (!valid) {
            return;
        }

        struct uint8_bitfield bitfield{connection_info};
        json_quic.print_key_value("connection_info", bitfield);
        json_quic.print_key_hex("version", version);
        json_quic.print_key_hex("dcid", dcid);
        json_quic.print_key_hex("scid", scid);
        json_quic.print_key_hex("token", token);
        json_quic.print_key_hex("data", payload);

    }

    constexpr static mask_and_value<8> matcher = {
       { 0b10110000, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x00 },
       { 0b10000000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
    };

};

class quic_parameters {
    std::unordered_map<uint32_t, uint8_t*> quic_initial_salt;

    uint8_t salt_d22[20]     = {0x7f,0xbc,0xdb,0x0e,0x7c,0x66,0xbb,0xe9,0x19,0x3a,0x96,0xcd,0x21,0x51,0x9e,0xbd,0x7a,0x02,0x64,0x4a};
    uint8_t salt_d23_d28[20] = {0xc3,0xee,0xf7,0x12,0xc7,0x2e,0xbb,0x5a,0x11,0xa7,0xd2,0x43,0x2b,0xb4,0x63,0x65,0xbe,0xf9,0xf5,0x02};
    uint8_t salt_d29_d32[20] = {0xaf,0xbf,0xec,0x28,0x99,0x93,0xd2,0x4c,0x9e,0x97,0x86,0xf1,0x9c,0x61,0x11,0xe0,0x43,0x90,0xa8,0x99};
    uint8_t salt_d33_v1[20]  = {0x38,0x76,0x2c,0xf7,0xf5,0x59,0x34,0xb3,0x4d,0x17,0x9a,0xe6,0xa4,0xc8,0x0c,0xad,0xcc,0xbb,0x7f,0x0a};


public:

    quic_parameters() {
        quic_initial_salt = {
            {4278190102, salt_d22},     // draft-22
            {4278190103, salt_d23_d28}, // draft-23
            {4278190104, salt_d23_d28}, // draft-24
            {4278190105, salt_d23_d28}, // draft-25
            {4278190106, salt_d23_d28}, // draft-26
            {4278190107, salt_d23_d28}, // draft-27
            {4278190108, salt_d23_d28}, // draft-28
            {4278190109, salt_d29_d32}, // draft-29
            {4278190110, salt_d29_d32}, // draft-30
            {4278190111, salt_d29_d32}, // draft-31
            {4278190112, salt_d29_d32}, // draft-32
            {4278190113, salt_d33_v1},  // draft-33
            {4278190114, salt_d33_v1},  // draft-34
            {1,          salt_d33_v1},  // version-1
        };
    }

    uint8_t *get_initial_salt(uint32_t version) {
        auto pair = quic_initial_salt.find(version);
        if (pair != quic_initial_salt.end()) {
            return pair->second;
        } else {
            return nullptr;
        }
    }

    static quic_parameters &create() {
        static quic_parameters quic_params;
        return quic_params;
    }
};

class quic_crypto_engine {

    crypto_engine core_crypto;

    constexpr static const uint8_t client_in_label[] = "tls13 client in";
    constexpr static const uint8_t quic_key_label[]  = "tls13 quic key";
    constexpr static const uint8_t quic_iv_label[]   = "tls13 quic iv";
    constexpr static const uint8_t quic_hp_label[]   = "tls13 quic hp";

    size_t salt_length = 20;

    uint8_t quic_key[EVP_MAX_MD_SIZE] = {0};
    unsigned int quic_key_len = 0;

    uint8_t quic_iv[EVP_MAX_MD_SIZE] = {0};
    unsigned int quic_iv_len = 0;

    uint8_t quic_hp[EVP_MAX_MD_SIZE] = {0};
    unsigned int quic_hp_len = 0;

    uint8_t pn_length = 0;

    unsigned char plaintext[pt_buf_len] = {0};
    int16_t plaintext_len = 0;

public:

    datum decrypt(const quic_initial_packet &quic_pkt) {
        if (!quic_pkt.is_not_empty()) {
            return {nullptr, nullptr};
        }

        // copy the additional authenticated data into a buffer, where
        // it can be edited to remove header protection
        //
        data_buffer<1024> aad;
        // uint8_t aad_buffer[1024];
        // size_t aad_len = quic_pkt.aad_end - quic_pkt.aad_start;
        // if (aad_len < sizeof(aad_buffer)) {
        //     memcpy(aad_buffer, quic_pkt.aad_start, aad_len);
        // } else {
        //     fprintf(stderr, "warning: need aad_buffer[%zu], only have %zu\n", sizeof(aad_buffer), aad_len);
        // }
        // datum aad{aad_buffer, aad_buffer+aad_len};

        if (process_initial_packet(aad, quic_pkt) == false) {
            return {nullptr, nullptr};
        }
        decrypt__(aad.buffer, aad.length(),
                  quic_pkt.payload.data, quic_pkt.payload.length());
        return {plaintext, plaintext+plaintext_len};
    }

private:

    bool process_initial_packet(data_buffer<1024> &aad, const quic_initial_packet &quic_pkt) {
        if (!quic_pkt.is_not_empty()) {
            return false;
        }
        const uint8_t *dcid = quic_pkt.dcid.data;
        size_t dcid_len = quic_pkt.dcid.length();
        uint32_t version = ntohl(*((uint32_t*)quic_pkt.version.data));

        static quic_parameters &quic_params = quic_parameters::create();  // initialize on first use
        uint8_t *initial_salt = quic_params.get_initial_salt(version);
        if (initial_salt == nullptr) {
            return false;
        }

        uint8_t initial_secret[EVP_MAX_MD_SIZE];
        unsigned int initial_secret_len = 0;
        HMAC(EVP_sha256(), initial_salt, salt_length, dcid, dcid_len, initial_secret, &initial_secret_len);

        uint8_t c_initial_secret[EVP_MAX_MD_SIZE] = {0};
        unsigned int c_initial_secret_len = 0;
        core_crypto.kdf_tls13(initial_secret, initial_secret_len, client_in_label, sizeof(client_in_label)-1, 32, c_initial_secret, &c_initial_secret_len);
        core_crypto.kdf_tls13(c_initial_secret, c_initial_secret_len, quic_key_label, sizeof(quic_key_label)-1, 16, quic_key, &quic_key_len);
        core_crypto.kdf_tls13(c_initial_secret, c_initial_secret_len, quic_iv_label, sizeof(quic_iv_label)-1, 12, quic_iv, &quic_iv_len);
        core_crypto.kdf_tls13(c_initial_secret, c_initial_secret_len, quic_hp_label, sizeof(quic_hp_label)-1, 16, quic_hp, &quic_hp_len);

        // remove header protection (RFC9001, Section 5.4.1)
        //
        static constexpr size_t sample_offset = 4;
        uint8_t mask[32] = {0};
        core_crypto.ecb_encrypt(quic_hp,mask,quic_pkt.payload.data + sample_offset,16);
        pn_length = quic_pkt.connection_info ^ (mask[0] & 0x0f);
        pn_length = (pn_length & 0x03) + 1;

        aad.copy(quic_pkt.connection_info ^ (mask[0] & 0x0f));
        aad.copy(quic_pkt.aad_start + 1, (quic_pkt.aad_end - quic_pkt.aad_start) - 1);

        // reconstruct packet number
        //
        uint32_t packet_number = 0;
        for (int i=0; i<pn_length; i++) {
            packet_number *= 256;
            packet_number += mask[i+1] ^ quic_pkt.payload.data[i];
            aad.copy(quic_pkt.payload.data[i] ^ mask[i+1]);
        }

        // construct AEAD iv
        //
        for (uint8_t i = quic_iv_len-pn_length; i < quic_iv_len; i++) {
            quic_iv[i] ^= (mask[(i-(quic_iv_len-pn_length))+1] ^ *(quic_pkt.payload.data + (i-(quic_iv_len-pn_length))));
        }

        return true;
    }

    void decrypt__(const uint8_t *ad, unsigned int ad_len, const uint8_t *data, unsigned int length) {

        uint16_t cipher_len = length - pn_length;
        plaintext_len = core_crypto.gcm_decrypt(ad, ad_len, data+pn_length, cipher_len, quic_key, quic_iv, plaintext);
        if (plaintext_len == -1) {
            plaintext_len = 0;  // error; indicate that there is no plaintext in buffer
        }
        
        // reset buffer states after decryption 
        //
        quic_key_len = 0;
        quic_iv_len = 0;
        quic_hp_len = 0;
        pn_length = 0; 
    }

};

//   Version Negotiation Packet {
//     Header Form (1) = 1,
//     Unused (7),
//     Version (32) = 0,
//     Destination Connection ID Length (8),
//     Destination Connection ID (0..2040),
//     Source Connection ID Length (8),
//     Source Connection ID (0..2040),
//     Supported Version (32) ...,
//   }
//
struct quic_version_negotiation {
    uint8_t connection_info;
    struct datum dcid;
    struct datum scid;
    struct datum version_list;
    bool valid;

    quic_version_negotiation(struct datum &d) : connection_info{0}, dcid{}, scid{}, version_list{}, valid{false} {
        parse(d);
    }

    void parse(struct datum &d) {
        d.read_uint8(&connection_info);
        if ((connection_info & 0x80) != 0x80) {
            return;
        }
        d.skip(4);  // skip version, it's 00000000

        uint8_t dcid_length;
        d.read_uint8(&dcid_length);
        dcid.parse(d, dcid_length);

        uint8_t scid_length;
        d.read_uint8(&scid_length);
        scid.parse(d, scid_length);

        version_list = d;  // TODO: member function to get remainder

        if ((version_list.is_not_empty() == false) || (dcid.is_not_empty() == false)) {
            return;  // invalid or incomplete packet
        }
        valid = true;
    }

    bool is_not_empty() {
        return valid;
    }

    void write_json(struct json_object &o) const {
        if (!valid) {
            return;
        }

        struct uint8_bitfield bitfield{connection_info};
        o.print_key_value("connection_info", bitfield);
        o.print_key_hex("dcid", dcid);
        o.print_key_hex("scid", scid);
        json_array array{o, "versions"};
        datum tmp = version_list;
        while (tmp.is_not_empty()) {
            datum version;
            version.parse(version, 4);
            array.print_hex(version);
        }
        array.close();
    }

};

class padding {

public:
	padding(datum &) {
    }

	void write(FILE *f) {
		fprintf(f, "padding\n");
	}

private:

    // the function parse_consecutive_padding() reads consecutive padding
    // frames and reports their number; it might be handy if you want to
    // print out frames.
    //
    size_t parse_consecutive_padding(datum &d) {
        size_t pad_len = 0;
        while (true) {
            uint8_t type = 0;
            d.lookahead_uint8(&type);
            if (type != 0 || !d.is_not_empty()) {
                break;
            }
            d.skip(1);
            ++pad_len;
        }
        return pad_len;
    }
};

class ping {
public:
	ping(datum &) {}

	void write(FILE *f) {
		fprintf(f, "ping\n");
	}
};

class quic_frame {
    std::variant<std::monostate, padding, ping, ack, crypto, connection_close> frame;

public:

    quic_frame(datum &d) {
        uint8_t type = 0;
        if (d.read_uint8(&type) == false) {
            frame.emplace<std::monostate>();   // invalid; no data to read
        } else if (type == 0x06) {
            frame.emplace<crypto>(d);
        } else if (type == 0x1c) {
            frame.emplace<connection_close>(d);
        } else if (type == 0x00) {
            frame.emplace<padding>(d);
        } else if (type == 0x01) {
            frame.emplace<ping>(d);
        } else if (type == 0x02) {
            frame.emplace<ack>(d);
        } else {
            // fprintf(stderr, "unknown frame type %02x\n", type);  // TODO: report through JSON
            frame.emplace<std::monostate>();
        }
    }

    quic_frame() : frame{} { }

    bool is_valid() const {
        return std::holds_alternative<std::monostate>(frame) == false;
    }

    template <typename T>
    bool has_type() const {
        return std::holds_alternative<T>(frame) == true;
    }

    template <typename T>
    T *get_if() {
        return std::get_if<T>(&frame);
    }

    class write_visitor {
        FILE *f_;
    public:
        write_visitor(FILE *f) : f_{f} { }

        template <typename T> void operator()(T &x) { x.write(f_); }

        void operator()(std::monostate &) { }
    };

    void write(FILE *f) {
        std::visit(write_visitor{f}, frame);
    }

    class write_json_visitor {
        json_object &o;
    public:
        write_json_visitor(json_object &json) : o{json} { }

        template <typename T> void operator()(T &x) { x.write_json(o); }

        void operator()(padding &) { }
        void operator()(ping &) { }
        void operator()(crypto &) { }
        void operator()(std::monostate &) { }
    };

    void write_json(json_object &o) {
        std::visit(write_json_visitor{o}, frame);
    }

};

struct cryptographic_buffer
{
    uint64_t buf_len = 0;
    unsigned char buffer[pt_buf_len] = {}; // pt_buf_len - decryption buffer trim size for gcm_decrypt

    void extend(crypto& d)
    {
        if (d.offset() + d.length() <= sizeof(buffer)) {
            memcpy(buffer + d.offset(), d.data().data, d.length());
            if (d.offset() + d.length() > buf_len) {
                buf_len = d.offset() + d.length();
            }
        }
        // TODO: track segments to verify that all are present
    }

    bool is_valid()
    {
        return buf_len > 0;
    }
};

struct quic_hdr_fp {
    const datum &version;

    quic_hdr_fp(const datum &version_) : version{version_} {};
    
    void fingerprint(struct buffer_stream &buf) const {
        //add version
        //
        buf.write_char('(');
        buf.raw_as_hex(version.data, version.length());
        buf.write_char(')');
    } 
};

// class quic_init represents an initial quic message
//
class quic_init {
    quic_initial_packet initial_packet;
    quic_crypto_engine &quic_crypto;
    cryptographic_buffer crypto_buffer;
    tls_client_hello hello;
    datum plaintext;
    quic_frame cc;

public:

    quic_init(struct datum &d, quic_crypto_engine &quic_crypto_) : initial_packet{d}, quic_crypto{quic_crypto_}, crypto_buffer{}, hello{}, plaintext{} {

        plaintext = quic_crypto.decrypt(initial_packet);

        // parse plaintext as a sequence of frames
        //
        datum plaintext_copy = plaintext;
        while (plaintext_copy.is_not_empty()) {
            quic_frame frame{plaintext_copy};
            //frame.write(stderr);
            if (!frame.is_valid()) {
                break;
            }

            crypto *c = frame.get_if<crypto>();
            if (c && c->is_valid()) {
                crypto_buffer.extend(*c);
            }
            if (frame.has_type<connection_close>() || frame.has_type<ack>()) {
                cc = frame;
            }
        }
        if(crypto_buffer.is_valid()){
            struct datum d{crypto_buffer.buffer, crypto_buffer.buffer + crypto_buffer.buf_len};
            tls_handshake tls{d};
            hello.parse(tls.body);
            hello.is_quic_hello = true;
        }
    }

    bool is_not_empty() {
        return initial_packet.is_not_empty();
        // note: plaintext.is_not_empty() could be used in output logic
    }

    bool has_tls() {
        return hello.is_not_empty();
    }

    void write_json(struct json_object &record, bool metadata_output=false) {
        if (hello.is_not_empty()) {
            hello.write_json(record, metadata_output);
        }
        json_object quic_record{record, "quic"};
        initial_packet.write_json(quic_record);
        if (cc.is_valid()) {
            cc.write_json(quic_record);
        }
        quic_record.print_key_hex("plaintext", plaintext);
        // json_object frame_dump{record, "frame_dump"};
        // datum plaintext_copy = plaintext;
        // while (plaintext_copy.is_not_empty()) {
        //     quic_frame frame{plaintext_copy};
        //     frame.write_json(frame_dump);
        // }
        // frame_dump.close();
        quic_record.close();
    }

    void compute_fingerprint(struct fingerprint &fp) const {
        // fp format :(quic_version)(tls_fp);
        quic_hdr_fp hdr_fp(initial_packet.version);
        fp.add(hdr_fp);
        if (hello.is_not_empty()) {
            fp.set(hello, fingerprint_type_quic);
        }
    }

    bool do_analysis(const struct key &k_, struct analysis_context &analysis_, classifier *c_) {
        struct datum sn{NULL, NULL};
        hello.extensions.set_server_name(sn);

        analysis_.destination.init(sn, k_);

        return c_->analyze_fingerprint_and_destination_context(analysis_.fp, analysis_.destination, analysis_.result);
    }
};


#endif /* QUIC_H */
