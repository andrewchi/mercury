/*
 * bencode.cc
 *
 * Copyright (c) 2022 Cisco Systems, Inc. All rights reserved.
 * License at https://github.com/cisco/mercury/blob/master/LICENSE
 */

#include "bencode.h"

namespace bencoding {
    void blist::write_raw_features(writeable &w) {
        if (!valid) {
            return;
        }

        if (lookahead<list_or_dict_end> is_end{body}) {
            body = is_end.advance();
            return;
        }

        w.copy('[');

        bool first = true;
        while(body.is_not_empty()) {
            if (!first) {
                w.copy(',');
            } else {
                first = false;
            }

            bencoded_data value{body};
            value.write_raw_features(w);

            if (lookahead<list_or_dict_end> is_end{body}) {
                body = is_end.advance();
                break;
            }
        }
        w.copy(']');
    }

    void blist::write_json(struct json_object &o) {
        if (!valid) {
            return;
        }

        if (lookahead<list_or_dict_end> is_end{body}) {
            body = is_end.advance();
            return;
        }

        struct json_array a{o, "attributes"};

        while(body.is_not_empty()) {
            struct json_object items(a);
            bencoded_data value{body};
            value.write_json(items);
            items.close();

            if (lookahead<list_or_dict_end> is_end{body}) {
                body = is_end.advance();
                break;
            }
        }
        a.close();
    }

    void dictionary::write_raw_features(writeable &w) {

        if (!valid) {
            return;
        }

        if (lookahead<list_or_dict_end> is_end{body}) {
            body = is_end.advance();
            return;
        }

        w.copy('[');
        bool first = true;
        while(body.is_not_empty()) {
            if (!first) {
                w.copy(',');
            } else {
                first = false;
            }

            w.copy('[');
            byte_string key{body};
            key.write_raw_features(w);

            w.copy(',');

            bencoded_data value{body};
            value.write_raw_features(w);

            w.copy(']');

            if (lookahead<list_or_dict_end> is_end{body}) {
                body = is_end.advance();
                break;
            }
        }
        w.copy(']');
    }

    void dictionary::write_json(struct json_object &o) {

        if (!valid) {
            return;
        }
        
        if (lookahead<list_or_dict_end> is_end{tmp}) {
            tmp = is_end.advance();
            return;
        }
        
        struct json_array a{o, "attributes"};
        
        while(tmp.is_not_empty()) {
            struct json_object items(a);
            
            byte_string key{tmp};
            items.print_key_json_string("key", key.value());
            
            bencoded_data value{tmp};
            value.write_json(items);
            items.close();

            if (lookahead<list_or_dict_end> is_end{tmp}) {
                tmp = is_end.advance();
                break;
            }
        }
        a.close();
    }
}

void bencoded_data::write_raw_features(writeable &w) {
    if (!valid) {
        return;
    }

    if (lookahead<encoded<uint8_t>> type{body}) {
        if (type.value == 'i') {
            bencoding::bint integer(body);
            integer.write_raw_features(w);
        } else if (type.value >= '0' and type.value <= '9') {
            bencoding::byte_string str(body);
            str.write_raw_features(w);
        } else if (type.value == 'd') {
            bencoding::dictionary dict(body);
            dict.write_raw_features(w);
        } else if (type.value == 'l') {
            bencoding::blist list(body);
            list.write_raw_features(w);
        } else {
            // Not a bencoded data
            body.set_null();
        }
    }
}

void bencoded_data::write_json(struct json_object &o) {
    if (!valid) {
        return;
    }

    if (lookahead<encoded<uint8_t>> type{body}) {
        if (type.value == 'i') {
            bencoding::bint integer(body);
            integer.write_json(o);
        } else if (type.value >= '0' and type.value <= '9') {
            bencoding::byte_string str(body);
            str.write_json(o);
        } else if (type.value == 'd') {
            bencoding::dictionary dict(body);
            dict.write_json(o);
        } else if (type.value == 'l') {
            bencoding::blist list(body);
            list.write_json(o);
        } else {
            // Not a bencoded data
            body.set_null();
        }
    }
}
