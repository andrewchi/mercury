/*
 * http.h
 *
 * Copyright (c) 2021 Cisco Systems, Inc. All rights reserved.  License at
 * https://github.com/cisco/mercury/blob/master/LICENSE
 */


#ifndef HTTP_H
#define HTTP_H

#include <list>
#include <unordered_map>

#include "protocol.h"
#include "match.h"
#include "analysis.h"
#include "fingerprint.h"
#include "perfect_hash.h"
#include "newhttp.h"

struct http_headers : public datum {
    bool complete;

    http_headers() : datum{}, complete{false} {}

    void parse(struct datum &p) {
        unsigned char crlf[2] = { '\r', '\n' };

        data = p.data;
        while (p.length() > 0) {
            if (p.compare(crlf, sizeof(crlf)) == 0) {
                p.skip(sizeof(crlf));
                complete = true;
                break;  /* at end of headers */
            }
            if (p.skip_up_to_delim(crlf, sizeof(crlf)) == false) {
                break;
            }
        }
        data_end = p.data;
    }

    // parses the headers for end of headers while ignoring missing CR '\r' in delimiter for header fields
    //
    void parse_ignore_cr(struct datum &p) {
        unsigned char lf[1] = { '\n' };
        unsigned char crlf[2] = { '\r', '\n'};

        data = p.data;
        while (p.length() > 0) {
            if (p.compare(lf, sizeof(lf)) == 0 || p.compare(crlf, sizeof(crlf)) == 0) {
                complete = true;
                break;  /* at end of headers */
            }
            if (p.skip_up_to_delim(lf, sizeof(lf)) == false) {
                break;
            }
        }
        data_end = p.data;
    }

    void print_host(struct json_object &o, const char *key) const;
    void print_matching_name(struct json_object &o, const char *key, struct datum &name) const;
    void print_matching_name(struct json_object &o, const char *key, const char* name) const;
    void print_matching_names(struct json_object &o, perfect_hash<const char*> &ph) const;
    void print_ssdp_names_and_feature_string(struct json_object &o, data_buffer<2048>& feature_buf, bool metadata) const;

    void fingerprint(struct buffer_stream &buf, perfect_hash<bool> &fp_data) const;

    struct datum get_header(const char *header_name);
};

class token : public one_or_more<token>  {
public:
    inline static bool in_class(uint8_t c) {
        switch (c) {
        case ':':
            return false;
        default:
            return true;
        }
    }
};

class LWS {
public:

    LWS(struct datum &p) {
        while (p.is_readable() and (*p.data == '\t' or *p.data == ' ')) {
            p.data++;
        }
    }
};

class field_value : public one_or_more <field_value> {
public:
    inline static bool in_class(uint8_t c) {
        switch (c) {
        case '\r':
        case '\n':
            return false;
        default:
            return true;
        }
    }
};

class delimiter {
    datum delimit;
    unsigned char crlf[2] = { '\r', '\n' };
    unsigned char lf[1] = { '\n' };

public:
    delimiter(struct datum &p) : delimit{nullptr, nullptr} {
        parse(p);
    }

    void parse(struct datum &p) {
        if (p.compare_nbytes(crlf, sizeof(crlf))) {
            delimit.parse(p, sizeof(crlf));
        } else if (p.compare_nbytes(lf, sizeof(lf))) {
            delimit.parse(p, sizeof(lf));
        }
    }

    void write_json(json_object &rec) const {
        rec.print_key_json_string("delimiter", delimit);
    }

    bool is_valid() const {
        return delimit.is_not_empty();
    }
 
};

struct httpheader {
    datum body;
    token name;
    literal_byte<':'> colon;
    field_value value;
    delimiter delim;
    bool valid;

    httpheader(datum &d) :
    body{d},
    name{d},
    colon{d},
    value{d},
    delim{d} {
        body.data_end = value.data_end;
        valid = d.is_not_null();
    }

    void fingerprint(struct buffer_stream &buf, perfect_hash<bool> &fp_data) const {
        if (!is_valid()) {
            return;
        }

        bool include_name = false;
        const bool include_value = *(fp_data.lookup(name.data, name.length(), include_name));
        if (include_name) {
            if (include_value) {
                buf.write_char('(');
                buf.raw_as_hex(body.data, body.length());         // write {name, value}
                buf.write_char(')');
            } else {
                buf.write_char('(');
                buf.raw_as_hex(name.data, name.length()); // write {name}
                buf.write_char(')');
            }
        }
    }

    bool is_valid () const {
        return valid;
    }

    void write_json(json_array &a) const {
        if (!is_valid()) {
            return;
        }

        json_object hdr{a};
        hdr.print_key_json_string("name", name);
        hdr.print_key_json_string("value", value);
        delim.write_json(hdr);
        hdr.close();
    }
};

struct http_request : public base_protocol {
    struct datum method;
    struct datum uri;
    struct datum protocol;
    struct http_headers headers;
    datum body;

    static constexpr size_t max_body_length = 512;  // limit on number of bytes reported

    http_request(datum &p) : method{NULL, NULL}, uri{NULL, NULL}, protocol{NULL, NULL}, headers{} { parse(p); }
    datum user_agent;
    datum host;
    datum x_forwarded_for;
    datum via;
    datum upgrade;
    datum referer;

    std::vector<httpheader> headers;

    http_request(datum &p) :
    method{NULL, NULL},
    uri{NULL, NULL},
    protocol{NULL, NULL},
    user_agent{NULL, NULL},
    host{NULL, NULL},
    x_forwarded_for{NULL, NULL},
    via{NULL, NULL},
    upgrade{NULL, NULL},
    referer{NULL, NULL} {
        parse(p);
    }

    void parse(struct datum &p);

    bool is_not_empty() const { return protocol.is_not_empty(); }

    void write_json(struct json_object &record, bool output_metadata);

    void fingerprint(struct buffer_stream &b) const;

    void compute_fingerprint(class fingerprint &fp) const;

    bool do_analysis(const struct key &k_, struct analysis_context &analysis_, classifier *c);

    // weight 14 bitmask that matches all HTTP methods
    //
    static constexpr mask_and_value<8> matcher{
        { 0xe0, 0xe0, 0xe0, 0x80, 0x80, 0x80, 0x80, 0x80 },
        { 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00 }
    };

    static constexpr mask_and_value<8> get_matcher{
        { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
        { 'G',  'E',  'T',  ' ',  0x00, 0x00, 0x00, 0x00 }
    };

    static constexpr mask_and_value<8> post_matcher{
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 },
        { 'P',  'O',  'S',  'T',  ' ',  0x00, 0x00, 0x00 }
    };

    static constexpr mask_and_value<8> connect_matcher{
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
        { 'C',  'O',  'N',  'N',  'E',  'C',  'T',  ' ' }
    };

    static constexpr mask_and_value<8> put_matcher{
        { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
        { 'P',  'U',  'T',  ' ',  0x00, 0x00, 0x00, 0x00 }
    };

    static constexpr mask_and_value<8> head_matcher{
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 },
        { 'H',  'E',  'A',  'D',  ' ',  0x00, 0x00, 0x00 }
    };

};

struct http_response : public base_protocol {
    struct datum version;
    struct datum status_code;
    struct datum status_reason;
    struct http_headers headers;
    datum body;

    static constexpr size_t max_body_length = 512;  // limit on number of bytes reported

    http_response(datum &p) : version{NULL, NULL}, status_code{NULL, NULL}, status_reason{NULL, NULL}, headers{} { parse(p); }

    void parse(struct datum &p);

    bool is_not_empty() const { return status_code.is_not_empty(); }

    void write_json(struct json_object &record, bool metadata=false);

    void fingerprint(struct buffer_stream &buf) const;

    void compute_fingerprint(class fingerprint &fp) const;

    struct datum get_header(const char *header_name);

    static constexpr mask_and_value<8> matcher{
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00 },
        { 'H',  'T',  'T',  'P',  '/',  '1',  0x00, 0x00 }
    };

};

namespace {

    [[maybe_unused]] int http_request_fuzz_test(const uint8_t *data, size_t size) {
        struct datum request_data{data, data+size};
        char buffer_1[8192];
        struct buffer_stream buf_json(buffer_1, sizeof(buffer_1));
        char buffer_2[8192];
        struct buffer_stream buf_fp(buffer_2, sizeof(buffer_2));
        struct json_object record(&buf_json);

        http_request request{request_data};
        if (request.is_not_empty()) {
            request.write_json(record, true);
            request.fingerprint(buf_fp);
        }

        return 0;
    }

};

#endif /* HTTP_H */
