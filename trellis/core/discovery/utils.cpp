/*
 * Copyright (C) 2025 Agtonomy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "trellis/core/discovery/utils.hpp"

#include <sstream>

namespace trellis::core::discovery::utils {

namespace {

/**
 * @brief Check if a file descriptor with the given name exists in a FileDescriptorSet.
 *
 * @param fset_ The FileDescriptorSet to search.
 * @param fname_ The filename to search for.
 * @return true If the file is found.
 */
bool HasFile(const google::protobuf::FileDescriptorSet& fset_, const std::string& fname_) {
  for (auto findex = 0; findex < fset_.file_size(); ++findex) {
    if (fset_.file(findex).name() == fname_) {
      return (true);
    }
  }
  return (false);
}

/**
 * @brief Recursively collect all file descriptors required to describe a message.
 *
 * @param desc The protobuf message descriptor.
 * @param fset The FileDescriptorSet to populate.
 */
void GetFileDescriptorRecursive(const google::protobuf::Descriptor* desc, google::protobuf::FileDescriptorSet& fset) {
  if (desc == nullptr) return;
  const google::protobuf::FileDescriptor* fdesc = desc->file();

  for (auto dep = 0; dep < fdesc->dependency_count(); ++dep) {
    // iterate containing messages
    const google::protobuf::FileDescriptor* sfdesc = fdesc->dependency(dep);
    for (auto mtype = 0; mtype < sfdesc->message_type_count(); ++mtype) {
      const google::protobuf::Descriptor* desc = sfdesc->message_type(mtype);
      GetFileDescriptorRecursive(desc, fset);
    }

    // containing enums ?
    if (sfdesc->enum_type_count() > 0) {
      const google::protobuf::EnumDescriptor* edesc = sfdesc->enum_type(0);
      const google::protobuf::FileDescriptor* efdesc = edesc->file();

      if (!HasFile(fset, efdesc->name())) {
        google::protobuf::FileDescriptorProto* epdesc = fset.add_file();
        efdesc->CopyTo(epdesc);
      }
    }

    // containing services ?
    if (sfdesc->service_count() > 0) {
      const google::protobuf::ServiceDescriptor* svdesc = sfdesc->service(0);
      const google::protobuf::FileDescriptor* svfdesc = svdesc->file();

      if (!HasFile(fset, svfdesc->name())) {
        google::protobuf::FileDescriptorProto* svpdesc = fset.add_file();
        svfdesc->CopyTo(svpdesc);
      }
    }
  }

  if (HasFile(fset, fdesc->name())) return;

  google::protobuf::FileDescriptorProto* pdesc = fset.add_file();
  fdesc->CopyTo(pdesc);
  for (auto field = 0; field < desc->field_count(); ++field) {
    const google::protobuf::FieldDescriptor* fddesc = desc->field(field);
    const google::protobuf::Descriptor* desc = fddesc->message_type();
    GetFileDescriptorRecursive(desc, fset);
  }
}

}  // namespace

std::string GetHostname() {
  char hostname[HOST_NAME_MAX] = {0};
  gethostname(hostname, sizeof(hostname));
  return std::string(hostname);
}

std::string GetProtoMessageDescription(const google::protobuf::Message& msg_) {
  return GetProtoMessageDescription(msg_.GetDescriptor());
}

std::string GetProtoMessageDescription(const google::protobuf::Descriptor* desc) {
  google::protobuf::FileDescriptorSet pset;
  GetFileDescriptorRecursive(desc, pset);
  return pset.SerializeAsString();
}

std::string GetExecutablePath() {
  char path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);

  if (len == -1) {
    throw std::runtime_error("Failed to resolve /proc/self/exe");
  }

  path[len] = '\0';
  return std::string(path);
}

std::string GetBaseName(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return path;  // No directory separator found, return the entire path.
  }
  return path.substr(pos + 1);
}

std::string GetArgv0() {
  std::ifstream cmdline("/proc/self/cmdline");
  std::string argv0;

  if (cmdline) {
    std::getline(cmdline, argv0, '\0');  // Read until the first null terminator
  }

  return argv0;
}

Sample CreateProtoPubSubSample(const std::string& topic, const std::string& message_desc,
                               const std::string& message_name, bool publisher,
                               std::vector<std::string> memory_file_list) {
  Sample sample;
  sample.set_type(publisher ? discovery::publisher_registration : discovery::subscriber_registration);
  sample.mutable_topic()->set_hname(GetHostname());
  sample.mutable_topic()->set_pid(::getpid());
  sample.mutable_topic()->set_pname(GetExecutablePath());
  sample.mutable_topic()->set_uname(GetBaseName(sample.topic().pname()));
  std::stringstream counter;
  counter << std::chrono::steady_clock::now().time_since_epoch().count();
  sample.mutable_topic()->set_tid(counter.str());
  sample.mutable_topic()->set_tname(topic);
  sample.mutable_topic()->mutable_tdatatype()->set_name(message_name);
  sample.mutable_topic()->mutable_tdatatype()->set_encoding("proto");
  sample.mutable_topic()->mutable_tdatatype()->set_desc(message_desc);

  {
    auto* layer = sample.mutable_topic()->add_tlayer();
    layer->set_type(discovery::tl_shm);
    layer->set_version(1);
    if (memory_file_list.size() > 0) {
      layer->mutable_par_layer()->mutable_layer_par_shm()->mutable_memory_file_list()->Assign(memory_file_list.begin(),
                                                                                              memory_file_list.end());
    }
  }

  return sample;
}

Sample CreateServiceServerSample(uint16_t port, const std::string& service_name) {
  Sample sample;
  sample.set_type(discovery::service_registration);
  sample.mutable_service()->set_hname(GetHostname());
  sample.mutable_service()->set_pname(GetExecutablePath());
  sample.mutable_service()->set_uname(GetBaseName(sample.service().pname()));
  sample.mutable_service()->set_pid(::getpid());
  sample.mutable_service()->set_sname(service_name);
  std::stringstream counter;
  counter << std::chrono::steady_clock::now().time_since_epoch().count();
  sample.mutable_service()->set_sid(counter.str());
  sample.mutable_topic()->set_tid(counter.str());  // TODO (bsirang) clean this up but for now piggy back off that field
  sample.mutable_service()->set_tcp_port(port);
  return sample;
}

}  // namespace trellis::core::discovery::utils
