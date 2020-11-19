/*
 * pkt_proc_json.h
 *
 * Copyright (c) 2019 Cisco Systems, Inc. All rights reserved.  License at
 * https://github.com/cisco/mercury/blob/master/LICENSE
 */

#ifndef PKT_PROC_JSON_H
#define PKT_PROC_JSON_H

#include "pkt_proc.h"
#include "proto_identify.h"
#include "tcp.h"
#include "dns.h"
#include "tls.h"
#include "http.h"
#include "wireguard.h"
#include "ssh.h"
#include "dhcp.h"
#include "tcpip.h"
#include "eth.h"
#include "udp.h"
#include "quic.h"
#include "analysis.h"
#include "llq.h"
#include "buffer_stream.h"

void write_flow_key(struct json_object &o, const struct key &k) {
    if (k.ip_vers == 6) {
        const uint8_t *s = (const uint8_t *)&k.addr.ipv6.src;
        o.print_key_ipv6_addr("src_ip", s);

        const uint8_t *d = (const uint8_t *)&k.addr.ipv6.dst;
        o.print_key_ipv6_addr("dst_ip", d);

    } else {

        const uint8_t *s = (const uint8_t *)&k.addr.ipv4.src;
        o.print_key_ipv4_addr("src_ip", s);

        const uint8_t *d = (const uint8_t *)&k.addr.ipv4.dst;
        o.print_key_ipv4_addr("dst_ip", d);

    }

    o.print_key_uint8("protocol", k.protocol);
    o.print_key_uint16("src_port", k.src_port);
    o.print_key_uint16("dst_port", k.dst_port);

    // o.b->snprintf(",\"flowhash\":\"%016lx\"", std::hash<struct key>{}(k));
}


/*
 * struct pkt_proc_json_writer_llq represents a packet processing object
 * that writes out a JSON representation of fingerprints, metadata,
 * flow keys, and event time to a queue that is then written to a file
 * by a dedicated output thread.
 */
struct pkt_proc_json_writer_llq : public pkt_proc {
    struct ll_queue *llq;
    bool block;
    struct packet_filter pf;
    struct flow_table ip_flow_table;
    struct flow_table_tcp tcp_flow_table;
    struct tcp_reassembler reassembler;
    struct tcp_reassembler *reassembler_ptr;

    /*
     * pkt_proc_json_writer(outfile_name, mode, max_records)
     * initializes object to write a single JSON line containing the
     * flow key, time, fingerprints, and metadata to the output file
     * with the path outfile_name and mode passed as arguments; that
     * file is opened by this invocation, with that mode.  If
     * max_records is nonzero, then it defines the maximum number of
     * records (lines) per file; after that limit is reached, file
     * rotation will take place.
     */
    explicit pkt_proc_json_writer_llq(struct ll_queue *llq_ptr, const char *filter, bool blocking) :
        block{blocking},
        ip_flow_table{65536},
        tcp_flow_table{65536},
        reassembler{65536},
        reassembler_ptr{&reassembler} {

        llq = llq_ptr;
        if (packet_filter_init(&pf, filter) == status_err) {
            throw "could not initialize packet filter";
        }

#ifndef USE_TCP_REASSEMBLY
// #pragma message "omitting tcp reassembly; 'make clean' and recompile with OPTFLAGS=-DUSE_TCP_REASSEMBLY to use that option"
        reassembler_ptr = nullptr;
#else
// #pragma message "using tcp reassembly; 'make clean' and recompile to omit that option"
#endif

    }

    void apply(struct packet_info *pi, uint8_t *eth) override {
        struct llq_msg *msg = llq->init_msg(block, pi->ts.tv_sec, pi->ts.tv_nsec);
        if (msg) {
            size_t write_len = append_packet_json(msg->buf, LLQ_MSG_SIZE, eth, pi->len, &(msg->ts), reassembler_ptr);
            if (write_len > 0) {
                msg->send(write_len);
                llq->increment_widx();
            }
        }
    }

    void finalize() override {
        reassembler.count_all();
        tcp_flow_table.count_all();
    }

    void flush() override {

    }

    size_t append_packet_json(void *buffer,
                              size_t buffer_size,
                              uint8_t *packet,
                              size_t length,
                              struct timespec *ts,
                              struct tcp_reassembler *reassembler);

    void tcp_data_write_json(struct buffer_stream &buf,
                             struct datum &pkt,
                             const struct key &k,
                             struct tcp_packet &tcp_pkt,
                             struct timespec *ts,
                             struct tcp_reassembler *reassembler);

};

size_t pkt_proc_json_writer_llq::append_packet_json(void *buffer,
                                                    size_t buffer_size,
                                                    uint8_t *packet,
                                                    size_t length,
                                                    struct timespec *ts,
                                                    struct tcp_reassembler *reassembler) {

    struct buffer_stream buf{(char *)buffer, buffer_size};
    struct key k;
    struct datum pkt{packet, packet+length};
    size_t transport_proto = 0;
    size_t ethertype = 0;
    parser_process_eth(&pkt, &ethertype);
    switch(ethertype) {
    case ETH_TYPE_IP:
        parser_process_ipv4(&pkt, &transport_proto, &k);
        break;
    case ETH_TYPE_IPV6:
        parser_process_ipv6(&pkt, &transport_proto, &k);
        break;
    default:
        ;
    }
    if (transport_proto == 6) {
        struct tcp_packet tcp_pkt;
        tcp_pkt.parse(pkt);
        if (tcp_pkt.header == nullptr) {
            return 0;  // incomplete tcp header; can't process packet
        }
        tcp_pkt.set_key(k);
        if (tcp_pkt.is_SYN()) {
            tcp_flow_table.syn_packet(k, ts->tv_sec, ntohl(tcp_pkt.header->seq));
            if (select_tcp_syn) {
                struct json_object record{&buf};
                struct json_object fps{record, "fingerprints"};
                fps.print_key_value("tcp", tcp_pkt);
                fps.close();
                if (global_vars.metadata_output) {
                    tcp_pkt.write_json(fps);
                }
                // note: we could check for non-empty data field
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();
            }

        } else if (tcp_pkt.is_SYN_ACK()) {
            tcp_flow_table.syn_packet(k, ts->tv_sec, ntohl(tcp_pkt.header->seq));

#ifdef REPORT_SYN_ACK
            if (select_tcp_syn) {
                struct json_object record{&buf};
                struct json_object fps{record, "fingerprints"};
                fps.print_key_value("tcp_server", tcp_pkt);
                fps.close();
                if (global_vars.metadata_output) {
                    tcp_pkt.write_json(fps);
                }
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();

                // note: we could check for non-empty data field
            }
#endif

        } else {

            // fprintf(stderr, "ip_flow_table.table.size(): %zu\n", ip_flow_table.table.size());
            // fprintf(stderr, "reassembler->segment_table.size(): %zu\n", reassembler->segment_table.size());

            if (reassembler) {
                const struct tcp_segment *data_buf = reassembler->check_packet(k, ts->tv_sec, tcp_pkt.header, pkt.length());
                if (data_buf) {
                    //fprintf(stderr, "REASSEMBLED TCP PACKET (length: %u)\n", data_buf->index);
                    struct datum reassembled_tcp_data = data_buf->reassembled_segment();
                    tcp_data_write_json(buf, reassembled_tcp_data, k, tcp_pkt, ts, reassembler);
                    reassembler->remove_segment(k);
                } else {
                    const uint8_t *tmp = pkt.data;
                    tcp_data_write_json(buf, pkt, k, tcp_pkt, ts, reassembler);
                    if (pkt.data == tmp) {
                        auto segment = reassembler->reap(ts->tv_sec);
                        if (segment != reassembler->segment_table.end()) {
                            //fprintf(stderr, "EXPIRED PARTIAL TCP PACKET (length: %u)\n", segment->second.index);
                            struct datum reassembled_tcp_data = segment->second.reassembled_segment();
                            tcp_data_write_json(buf, reassembled_tcp_data, segment->first, tcp_pkt, ts, nullptr);
                            reassembler->remove_segment(segment);
                        }
                    }
                }
            } else {
                tcp_data_write_json(buf, pkt, k, tcp_pkt, ts, nullptr);  // process packet without tcp reassembly
            }
        }

    } else if (transport_proto == 17) {
        struct udp_packet udp_pkt;
        udp_pkt.parse(pkt);
        udp_pkt.set_key(k);
        bool is_new = false;
        if (global_vars.output_udp_initial_data && pkt.is_not_empty()) {
            is_new = ip_flow_table.flow_is_new(k, ts->tv_sec);
        }
        enum udp_msg_type msg_type = udp_get_message_type(pkt.data, pkt.length());
        if (msg_type == udp_msg_type_unknown) {
            msg_type = udp_pkt.estimate_msg_type_from_ports();
        }
        switch(msg_type) {
        case udp_msg_type_quic:
            {
                struct quic_initial_packet quic_pkt{pkt};
                if (quic_pkt.is_not_empty()) {
                    struct json_object json_record{&buf};
                    struct quic_initial_packet_crypto quic_pkt_crypto{quic_pkt};
                    quic_pkt_crypto.decrypt(quic_pkt.data.data, quic_pkt.data.length());
                    if (quic_pkt_crypto.is_not_empty()) {
                        struct tls_client_hello hello;
                        struct datum quic_plaintext(quic_pkt_crypto.plaintext+8, quic_pkt_crypto.plaintext+quic_pkt_crypto.plaintext_len);
                        hello.parse(quic_plaintext);
                        if (hello.is_not_empty()) {
                            struct json_object fps{json_record, "fingerprints"};
                            fps.print_key_value("quic", hello);
                            fps.close();
                            hello.write_json(json_record, global_vars.metadata_output);
                        }
                    }
                    struct json_object json_quic{json_record, "quic"};
                    quic_pkt.write_json(json_quic);
                    json_quic.close();
                    write_flow_key(json_record, k);
                    json_record.print_key_timestamp("event_start", ts);
                    json_record.close();
                }
            }
            break;
        case udp_msg_type_wireguard:
            {
                wireguard_handshake_init wg;
                wg.parse(pkt);
                struct json_object record{&buf};
                wg.write_json(record);
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();
            }
            break;
        case udp_msg_type_dns:
            {
                if (global_vars.dns_json_output) {
                    struct dns_packet dns_pkt{pkt};
                    if (dns_pkt.is_not_empty()) {
                        struct json_object json_record{&buf};
                        struct json_object json_dns{json_record, "dns"};
                        dns_pkt.write_json(json_dns);
                        json_dns.close();
                        write_flow_key(json_record, k);
                        json_record.print_key_timestamp("event_start", ts);
                        json_record.close();
                    }
                } else {
                    struct json_object json_record{&buf};
                    struct json_object json_dns{json_record, "dns"};
                    json_dns.print_key_base64("base64", pkt);
                    json_dns.close();
                    write_flow_key(json_record, k);
                    json_record.print_key_timestamp("event_start", ts);
                    json_record.close();
                }
            }
            break;
        case udp_msg_type_dtls_client_hello:
            {
                struct dtls_record dtls_rec;
                dtls_rec.parse(pkt);
                struct dtls_handshake handshake;
                handshake.parse(dtls_rec.fragment);
                if (handshake.msg_type == handshake_type::client_hello) {
                    struct tls_client_hello hello;
                    hello.parse(handshake.body);
                    if (hello.is_not_empty()) {
                        struct json_object record{&buf};
                        struct json_object fps{record, "fingerprints"};
                        fps.print_key_value("dtls", hello);
                        fps.close();
                        hello.write_json(record, global_vars.metadata_output);
                        write_flow_key(record, k);
                        record.print_key_timestamp("event_start", ts);
                        record.close();
                    }
                }
            }
            break;
        case udp_msg_type_dhcp:
            {
                struct dhcp_discover dhcp_disco;
                dhcp_disco.parse(pkt);
                if (dhcp_disco.is_not_empty()) {
                    struct json_object record{&buf};
                    struct json_object fps{record, "fingerprints"};
                    fps.print_key_value("dhcp", dhcp_disco);
                    fps.close();
                    if (global_vars.metadata_output) {
                        dhcp_disco.write_json(record);
                        write_flow_key(record, k);
                        record.print_key_timestamp("event_start", ts);
                    }
                    record.close();
                }
            }
            break;
        case udp_msg_type_dtls_server_hello:
        case udp_msg_type_dtls_certificate:
            // cases that fall through here are not yet supported
        case udp_msg_type_unknown:
            if (is_new) {
                struct json_object record{&buf};
                struct json_object udp{record, "udp"};
                udp.print_key_hex("data", pkt);
                // udp.print_key_json_string("data_string", pkt);
                udp.close();
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();
            }
            break;
        }
    }

    if (buf.length() != 0 && buf.trunc == 0) {
        buf.strncpy("\n");
        return buf.length();
    }
    return 0;
}

// tcp_data_write_json() parses TCP data and writes metadata into
// a buffer stream, if any is found
//
void pkt_proc_json_writer_llq::tcp_data_write_json(struct buffer_stream &buf,
                                                   struct datum &pkt,
                                                   const struct key &k,
                                                   struct tcp_packet &tcp_pkt,
                                                   struct timespec *ts,
                                                   struct tcp_reassembler *reassembler) {

    if (pkt.is_not_empty() == false) {
        return;
    }
    enum tcp_msg_type msg_type = get_message_type(pkt.data, pkt.length());

    bool is_new = false;
    if (global_vars.output_tcp_initial_data) {
        is_new = tcp_flow_table.is_first_data_packet(k, ts->tv_sec, ntohl(tcp_pkt.header->seq));
    }

    switch(msg_type) {
    case tcp_msg_type_http_request:
        {
            struct http_request request;
            request.parse(pkt);
            if (request.is_not_empty()) {
                struct json_object record{&buf};
                struct json_object fps{record, "fingerprints"};
                fps.print_key_value("http", request);
                fps.close();
                record.print_key_string("complete", request.headers.complete ? "yes" : "no");
                request.write_json(record, global_vars.metadata_output);
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();
            }
        }
        break;
    case tcp_msg_type_tls_client_hello:
        {
            struct tls_record rec;
            rec.parse(pkt);
            struct tls_handshake handshake;
            handshake.parse(rec.fragment);
            if (handshake.additional_bytes_needed && reassembler) {
                // fprintf(stderr, "tls.handshake.client_hello (%zu)\n", handshake.additional_bytes_needed);
                if (reassembler->copy_packet(k, ts->tv_sec, tcp_pkt.header, tcp_pkt.data_length, handshake.additional_bytes_needed)) {
                    return;
                }
            }
            struct tls_client_hello hello;
            hello.parse(handshake.body);
            if (hello.is_not_empty()) {
                struct json_object record{&buf};
                struct json_object fps{record, "fingerprints"};
                fps.print_key_value("tls", hello);
                fps.close();
                hello.write_json(record, global_vars.metadata_output);
                /*
                 * output analysis (if it's configured)
                 */
                if (global_vars.do_analysis) {
                    write_analysis_from_extractor_and_flow_key(buf, hello, k);
                }
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();
            }
        }
        break;
    case tcp_msg_type_tls_server_hello:
    case tcp_msg_type_tls_certificate:
        {
            struct tls_record rec;
            struct tls_handshake handshake;
            struct tls_server_hello hello;
            struct tls_server_certificate certificate;

            // parse server_hello and/or certificate
            //
            rec.parse(pkt);
            handshake.parse(rec.fragment);
            if (handshake.msg_type == handshake_type::server_hello) {
                hello.parse(handshake.body);
                if (rec.is_not_empty()) {
                    struct tls_handshake h;
                    h.parse(rec.fragment);
                    certificate.parse(h.body);
                }

            } else if (handshake.msg_type == handshake_type::certificate) {
                certificate.parse(handshake.body);
            }
            struct tls_record rec2;
            rec2.parse(pkt);
            struct tls_handshake handshake2;
            handshake2.parse(rec2.fragment);
            if (handshake2.msg_type == handshake_type::certificate) {
                certificate.parse(handshake2.body);
            }

            if (certificate.additional_bytes_needed && reassembler) {
                // fprintf(stderr, "tls.handshake.certificate (%zu)\n", certificate.additional_bytes_needed);
                if (reassembler->copy_packet(k, ts->tv_sec, tcp_pkt.header, tcp_pkt.data_length, certificate.additional_bytes_needed)) {
                    return;
                }
            }

            bool have_hello = hello.is_not_empty();
            bool have_certificate = certificate.is_not_empty();
            if (have_hello || have_certificate) {
                struct json_object record{&buf};

                // output fingerprint
                if (have_hello) {
                    struct json_object fps{record, "fingerprints"};
                    fps.print_key_value("tls_server", hello);
                    fps.close();
                }

                // output certificate (always) and server_hello (if configured to)
                //
                if ((global_vars.metadata_output && have_hello) || have_certificate) {
                    struct json_object tls{record, "tls"};
                    struct json_object tls_server{tls, "server"};
                    if (have_certificate) {
                        struct json_array server_certs{tls_server, "certs"};
                        certificate.write_json(server_certs, global_vars.certs_json_output);
                        server_certs.close();
                    }
                    if (global_vars.metadata_output && have_hello) {
                        hello.write_json(tls_server);
                    }
                    tls_server.close();
                    tls.close();
                }
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();
            }
        }
        break;
    case tcp_msg_type_http_response:
        {
            struct http_response response;
            response.parse(pkt);
            if (response.is_not_empty()) {
                struct json_object record{&buf};
                struct json_object fps{record, "fingerprints"};
                fps.print_key_value("http_server", response);
                fps.close();
                record.print_key_string("complete", response.headers.complete ? "yes" : "no");
                if (global_vars.metadata_output) {
                    response.write_json(record);
                }
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();
            }
        }
        break;
    case tcp_msg_type_ssh:
        {
            struct ssh_init_packet init_packet;
            init_packet.parse(pkt);
            struct json_object record{&buf};
            struct json_object fps{record, "fingerprints"};
            fps.print_key_value("ssh", init_packet);
            fps.close();
            init_packet.write_json(record, global_vars.metadata_output);
#ifdef SSHM
            if (pkt.is_not_empty()) {
                pkt.accept('\n');
                record.print_key_json_string("ssh_residual_data", pkt.data, pkt.length());
                struct ssh_binary_packet bin_pkt;
                bin_pkt.parse(pkt);
                struct ssh_kex_init kex_init;
                kex_init.parse(bin_pkt.payload);
                kex_init.write_json(record, global_vars.metadata_output);
            }
#endif
            write_flow_key(record, k);
            record.print_key_timestamp("event_start", ts);
            record.close();
        }
        break;
    case tcp_msg_type_ssh_kex:
        {
            struct ssh_binary_packet ssh_pkt;
            ssh_pkt.parse(pkt);
            if (ssh_pkt.additional_bytes_needed && reassembler) {
                // fprintf(stderr, "ssh.binary_packet (%zu)\n", ssh_pkt.additional_bytes_needed);
                if (reassembler->copy_packet(k, ts->tv_sec, tcp_pkt.header, tcp_pkt.data_length, ssh_pkt.additional_bytes_needed)) {
                    return;
                }
            }
            struct ssh_kex_init kex_init;
            kex_init.parse(ssh_pkt.payload);
            if (kex_init.is_not_empty()) {
                struct json_object record{&buf};
                struct json_object fps{record, "fingerprints"};
                fps.print_key_value("ssh_kex", kex_init);
                fps.close();
                kex_init.write_json(record, global_vars.metadata_output);
                write_flow_key(record, k);
                record.print_key_timestamp("event_start", ts);
                record.close();
            }
        }
        break;
    case tcp_msg_type_unknown:

        if (is_new) {
            // if this packet is a TLS record, ignore it
            if (tls_record::is_valid(pkt)) {
                return;
            }

            // output the data field
            struct json_object record{&buf};
            struct json_object tcp{record, "tcp"};
            tcp.print_key_hex("data", pkt);
            tcp.close();
            write_flow_key(record, k);
            record.print_key_timestamp("event_start", ts);
            record.close();
        }
        break;
    }

}

#endif /* PKT_PROC_JSON */