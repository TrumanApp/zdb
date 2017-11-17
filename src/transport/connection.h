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

#ifndef ZDB_CONNECTION_H
#define ZDB_CONNECTION_H

#include <string>
#include <json/json.h>

namespace zdb {

struct Message {
  enum class Status {
    OK,
    WOULD_BLOCK,
    ERROR,
  };
  Status status = Status::ERROR;
  std::string data;
  std::string error;

  Message();
  Message(const Message&);
  Message(Message&&);
  ~Message();

  Message& operator=(const Message&);
};

enum class ClientType {
  UNKNOWN,
  KAFKA_PRODUCER,
  KAFKA_CONSUMER,
  NANOMSG_PRODUCER,
  NANOMSG_CONSUMER,
};

class Connection {
 public:
  Connection() = default;

  virtual bool connect(const Json::Value& transport_config,
               const std::string& topic,
               const std::string& client_id) = 0;

  bool connected() const { return m_connected; };

  virtual ClientType client_type() const = 0;

  const std::string& topic_name() const { return m_topic_name; }

 protected:
  bool m_connected = false;
  std::string m_topic_name;

 private:
  Connection(const Connection&) = delete;
};

class ConsumerConnection : virtual public Connection {
 public:
  ConsumerConnection();

  virtual Message consume() = 0;
};

class ProducerConnection : virtual public Connection {
 public:
  ProducerConnection();

  virtual Message produce(const std::string& msg) = 0;
  virtual Message produce_blocking(const std::string& msg, size_t max_attempts) = 0;
};

}  // namespace zdb

#endif /* ZDB_CONNECTION_H */
