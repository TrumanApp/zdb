/*
 * ZDB Copyright 2017 Regents of the University of Michigan
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <fstream>
#include <time.h>
#include <iomanip>
#include <ctime>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <net/ethernet.h>
#include <netdb.h>
#include <net/if.h>
#include <ifaddrs.h>  // NOTE: net/if.h MUST be included BEFORE ifaddrs.h
#include <arpa/inet.h>

#include <set>
#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "anonymous_store.h"
#include "utility.h"
#include "record.h"
#include "zmap/logger.h"
#include "search.grpc.pb.h"
#include "store.h"
#include "inbound.h"
#include "protocol_names.h"
#include <json/json.h>

#include "base64/base64.h"

using namespace zdb;
using namespace zsearch;

void make_tags_string(std::ostream& f, const std::set<std::string>& tags) {
    f << '[';
    bool first = true;
    for (const auto& t : tags) {
        if (first) {
            first = false;
        } else {
            f << ',';
        }
        f << '\"' << t << '\"';
    }
    f << ']';
}

void make_metadata_string(std::ostream& f,
                          const std::map<std::string, std::string>& globals) {
    f << "{";
    bool first = true;
    for (const auto& g : globals) {
        if (first) {
            first = false;
        } else {
            f << ',';
        }
        f << '\"' << g.first << "\":" << '\"' << g.second << '\"';
    }
    f << "}";
}

std::string time_to_string(uint32_t rt) {
    time_t raw = (time_t) rt;
    std::time_t t = std::time(&raw);

    char buf[1024];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return std::string(buf);
}

std::string translate_certificate_source(int source) {
    switch (source) {
        case 0:
            // zero in the ENUM means reserved. However, we have many records
            // where
            // source is zero from before we track certificate sources. Until we
            // rewrite all of these records, zero will have to mean a
            // certificate from
            // a scan.
            return "reserved";
        case 1:
            return "unknown";
        case 2:
            return "scan";
        case 3:
            return "ct";
        case 4:
            return "mozilla_salesforce";
        case 5:
            return "research";
        case 6:
            return "rapid7";
        case 7:
            return "hubble";
        default:
            return "unknown";
    }
}

void fast_dump_ct_server(std::ostream& f, const zsearch::CTServerStatus ctss) {
    f << "{";
    if (ctss.index() > 0) {
        f << "\"index\":" << ctss.index();
        f << ",\"added_to_ct_at\":\"" << time_to_string(ctss.ct_timestamp())
          << "\"";
        f << ",\"ct_to_censys_at\":\"" << time_to_string(ctss.pull_timestamp())
          << "\"";
        if (ctss.push_timestamp()) {
            f << ",\"censys_to_ct_at\":\""
              << time_to_string(ctss.push_timestamp()) << "\"";
        }
    }
    f << "}";
}

void fast_dump_ct(std::ostream& f, const zsearch::CTStatus cts) {
    f << "{";
    f << "\"censys_dev\":";
    fast_dump_ct_server(f, cts.censys_dev());
    f << ",\"censys\":";
    fast_dump_ct_server(f, cts.censys());
    f << ",\"google_aviator\":";
    fast_dump_ct_server(f, cts.google_aviator());
    f << ",\"google_daedalus\":";
    fast_dump_ct_server(f, cts.google_daedalus());
    f << ",\"google_pilot\":";
    fast_dump_ct_server(f, cts.google_pilot());
    f << ",\"google_rocketeer\":";
    fast_dump_ct_server(f, cts.google_rocketeer());
    f << ",\"google_icarus\":";
    fast_dump_ct_server(f, cts.google_icarus());
    f << ",\"google_skydiver\":";
    fast_dump_ct_server(f, cts.google_skydiver());
    f << ",\"google_submariner\":";
    fast_dump_ct_server(f, cts.google_submariner());
    f << ",\"google_testtube\":";
    fast_dump_ct_server(f, cts.google_testtube());
    f << ",\"digicert_ct1\":";
    fast_dump_ct_server(f, cts.digicert_ct1());
    f << ",\"digicert_ct2\":";
    fast_dump_ct_server(f, cts.digicert_ct2());
    f << ",\"izenpe_com_ct\":";
    fast_dump_ct_server(f, cts.izenpe_com_ct());
    f << ",\"izenpe_eus_ct\":";
    fast_dump_ct_server(f, cts.izenpe_eus_ct());
    f << ",\"symantec_ws_ct\":";
    fast_dump_ct_server(f, cts.symantec_ws_ct());
    f << ",\"symantec_ws_vega\":";
    fast_dump_ct_server(f, cts.symantec_ws_vega());
    f << ",\"symantec_ws_deneb\":";
    fast_dump_ct_server(f, cts.symantec_ws_deneb());
    f << ",\"symantec_ws_sirius\":";
    fast_dump_ct_server(f, cts.symantec_ws_sirius());
    f << ",\"wosign_ctlog\":";
    fast_dump_ct_server(f, cts.wosign_ctlog());
    f << ",\"cnnic_ctserver\":";
    fast_dump_ct_server(f, cts.cnnic_ctserver());
    f << ",\"gdca_ct\":";
    fast_dump_ct_server(f, cts.gdca_ct());
    f << ",\"startssl_ct\":";
    fast_dump_ct_server(f, cts.startssl_ct());
    f << ",\"venafi_api_ctlog\":";
    fast_dump_ct_server(f, cts.venafi_api_ctlog());
    f << ",\"venafi_api_ctlog_gen2\":";
    fast_dump_ct_server(f, cts.venafi_api_ctlog_gen2());
    f << ",\"nordu_ct_plausible\":";
    fast_dump_ct_server(f, cts.nordu_ct_plausible());
    f << ",\"comodo_dodo\":";
    fast_dump_ct_server(f, cts.comodo_dodo());
    f << ",\"comodo_mammoth\":";
    fast_dump_ct_server(f, cts.comodo_mammoth());
    f << ",\"comodo_sabre\":";
    fast_dump_ct_server(f, cts.comodo_sabre());
    f << ",\"gdca_ctlog\":";
    fast_dump_ct_server(f, cts.gdca_ctlog());
    f << ",\"sheca_ct\":";
    fast_dump_ct_server(f, cts.sheca_ct());
    f << ",\"certificatetransparency_cn_ct\":";
    fast_dump_ct_server(f, cts.certificatetransparency_cn_ct());
    f << ",\"letsencrypt_ct_clicky\":";
    fast_dump_ct_server(f, cts.letsencrypt_ct_clicky());

    f << "}";
}

void fast_dump_certificate(std::ostream& f,
                           std::map<std::string, std::string>& metadata,
                           std::set<std::string>& tags,
                           const zsearch::Certificate& certificate) {
    f << "{";
    f << "\"raw\":\"" << base64_encode(certificate.raw()) << "\"";
    if (certificate.parse_status() == CERTIFICATE_PARSE_STATUS_SUCCESS ||
            (certificate.parse_status() == CERTIFICATE_PARSE_STATUS_RESERVED
                && certificate.parsed() != "")) {
        f << ",\"parsed\":" << certificate.parsed();
    }
    if (metadata.size() > 0) {
        f << ",\"metadata\":";
        make_metadata_string(f, metadata);
    }
    if (tags.size() > 0) {
        f << ",\"tags\":";
        make_tags_string(f, tags);
    }
    f << ",\"valid_nss\":" << (certificate.valid_nss() ? "true" : "false");
    f << ",\"was_valid_nss\":"
      << (certificate.was_valid_nss() ? "true" : "false");
    f << ",\"current_valid_nss\":"
      << (certificate.current_valid_nss() ? "true" : "false");

    f << ",\"in_nss\":" << (certificate.in_nss() ? "true" : "false");
    f << ",\"was_in_nss\":" << (certificate.was_in_nss() ? "true" : "false");
    f << ",\"current_in_nss\":"
      << (certificate.current_in_nss() ? "true" : "false");

    // f << ",\"valid_apple\":" << (certificate.valid_apple() ? "true" :
    // "false");
    // f << ",\"valid_microsoft\":" << (certificate.valid_microsoft() ? "true" :
    // "false");

    if (certificate.parents_size() > 0) {
        f << ",\"parents\":[";
        bool first = true;
        for (auto& p : certificate.parents()) {
            if (first) {
                first = false;
            } else {
                f << ",";
            }
            f << "\"" << hex_encode(p) << "\"";
        }
        f << "]";
    }

    if (certificate.validation_timestamp() > 0) {
        time_t raw = (time_t) certificate.validation_timestamp();
        std::time_t t = std::time(&raw);
        char buf[1024];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                      std::localtime(&t));
        f << ",\"validation_timestamp\":\"" << buf << "\"";
    }
    // calculate whether it's been seen in a scan based on whether the
    // certificate
    // originated from a scan or it has been since seen in a scan.
    bool seen_in_scan = false;
    if (certificate.seen_in_scan()) {
        seen_in_scan = true;
    } else if (certificate.source() == 0 || certificate.source() == 2) {
        seen_in_scan = true;
    }
    f << ",\"seen_in_scan\":" << (seen_in_scan ? "true" : "false");
    f << ",\"source\":\"" << translate_certificate_source(certificate.source())
      << "\"";

    f << ",\"ct\":";
    fast_dump_ct(f, certificate.ct());

    f << "}" << std::endl;
}

std::string dump_certificate_to_json_string(zsearch::AnonymousRecord rec) {
    std::map<std::string, std::string> metadata;
    std::set<std::string> tags;
    for (const auto& p : rec.tags()) {
        tags.insert(p);
    }
    for (const auto& p : rec.userdata().public_tags()) {
        tags.insert(p);
    }
    for (const auto& p : rec.metadata()) {
        metadata[p.key()] = p.value();
    }
    for (const auto& p : rec.userdata().public_metadata()) {
        metadata[p.key()] = p.value();
    }
    std::ostringstream f;
    fast_dump_certificate(f, metadata, tags, rec.certificate());
    return f.str();
}

void dump_proto_with_timestamp(std::ostream& f,
                               std::string data,
                               uint32_t timestamp) {
    // remove closing } and dump
    data.pop_back();
    f << data;
    // if no data, then don't need to add a comma
    if (data != "{") {
        f << ", ";
    }
    f << "\"timestamp\":\"" << time_to_string(timestamp) << "\"}";
}

void fast_dump_ipv4_host(std::ostream& f,
                         uint32_t ip,
                         std::string domain,
                         std::vector<zsearch::Record>& records,
                         std::map<std::string, std::string>& metadata,
                         std::set<std::string>& tags,
                         zsearch::LocationAtom& public_location,
                         zsearch::LocationAtom& private_location,
                         zsearch::ASAtom& as_data,
                         bool public_location_found,
                         bool private_location_found,
                         bool as_data_found,
                         int alexa_rank,
                         Json::FastWriter& fastWriter) {
    auto has_protocol_atom = std::find_if(records.begin(), records.end(),
                                          [](const zsearch::Record& r) {
                                              switch (r.data_oneof_case()) {
                                                  case zsearch::Record::kAtom:
                                                      return true;
                                                  default:
                                                      break;
                                              }
                                              return false;
                                          });
    if (has_protocol_atom == records.end()) {
        return;
    }

    std::string ipstr = make_ip_str(ip);
    f << "{\"ip\":\"" << ipstr << "\",";
    f << "\"ipint\":" << ntohl(ip) << ",";
    if (!domain.empty()) {
        f << "\"domain\":\"" << domain << "\",";
    }
    if (alexa_rank > 0) {
        f << "\"alexa_rank\":" << alexa_rank << ",";
    }

    int lastport = -1;
    int lastproto = -1;
    int lastsubproto = -1;

    for (zsearch::Record& r : records) {
        // create new port clause
        if (r.port() != lastport) {
            if (lastport >= 0) {  // if there was a previous port, must close
                f << "}},";
            }
            f << "\"p" << ntohs((uint16_t) r.port())
              << "\":{";     // great we're in a new port clause
            lastproto = -1;  // mark this as the first protocol in the series
            lastsubproto = -1;
        }
        if (r.protocol() != lastproto) {
            if (lastproto >= 0) {  // if there was a previous proto, must close
                f << "},";
            }
            // great now we can add a new protocol
            f << "\"" << get_proto_name(r.protocol()) << "\":{";
            lastsubproto = -1;
        }
        // ok now we're in a protocol. woo. add a subprotocol. If there was
        // a previous subprotocol in this protocol, we must add a comma,
        if (lastsubproto >= 0) {
            f << ",";
        }
        // we've taken care of anything before. now we can add the subproto
        f << "\"" << get_subproto_name(r.protocol(), r.subprotocol()) << "\":";
        dump_proto_with_timestamp(f, r.atom().data(), r.timestamp());

        lastport = r.port();
        lastproto = r.protocol();
        lastsubproto = r.subprotocol();
    }
    // we need to close both proto and subproto
    f << "}}";
    // records done. add metadata and tags
    if (metadata.size() > 0) {
        f << ",\"metadata\":";
        make_metadata_string(f, metadata);
    }
    if (tags.size() > 0) {
        f << ",\"tags\":";
        make_tags_string(f, tags);
    }
    if (public_location_found && public_location.latitude() &&
        public_location.longitude()) {
        f << ",\"location\":";
        auto public_location_json = Json::Value(Json::objectValue);
        public_location_json["continent"] = public_location.continent();
        public_location_json["country"] = public_location.country();
        public_location_json["country_code"] = public_location.country_code();
        public_location_json["city"] = public_location.city();
        public_location_json["postal_code"] = public_location.postal_code();
        public_location_json["timezone"] = public_location.timezone();
        public_location_json["province"] = public_location.province();
        public_location_json["latitude"] = public_location.latitude();
        public_location_json["longitude"] = public_location.longitude();
        public_location_json["registered_country"] =
                public_location.registered_country();
        public_location_json["registered_country_code"] =
                public_location.registered_country_code();
        f << fastWriter.write(public_location_json);
    }
    // if (private_location_found) {
    //    f << ",\"__restricted_location\":";
    //    auto private_location_json = Json::Value(Json::objectValue);
    //    private_location_json["continent"] = private_location.continent();
    //    private_location_json["country"] = private_location.country();
    //    private_location_json["country_code"] =
    //    private_location.country_code();
    //    private_location_json["city"] = private_location.city();
    //    private_location_json["postal_code"] = private_location.postal_code();
    //    private_location_json["timezone"] = private_location.timezone();
    //    private_location_json["province"] = private_location.province();
    //    private_location_json["latitude"] = private_location.latitude();
    //    private_location_json["longitude"] = private_location.longitude();
    //    private_location_json["registered_country"] =
    //    private_location.registered_country();
    //    private_location_json["registered_country_code"] =
    //    private_location.registered_country_code();
    //    f << fastWriter.write(private_location_json);
    //}

    if (as_data_found) {
        f << ",\"autonomous_system\":";
        auto as_json = Json::Value(Json::objectValue);
        as_json["asn"] = as_data.asn();
        as_json["description"] = as_data.description();
        as_json["path"] = Json::Value(Json::arrayValue);
        for (const auto& path_elt : as_data.path()) {
            as_json["path"].append(path_elt);
        }
        // as_json["rir"] =
        as_json["routed_prefix"] = as_data.bgp_prefix();
        as_json["name"] = as_data.name();
        as_json["country_code"] = as_data.country_code();
        as_json["organization"] = as_data.organization();
        f << fastWriter.write(as_json);
    }
    // close IP address
    f << "}\n";
}