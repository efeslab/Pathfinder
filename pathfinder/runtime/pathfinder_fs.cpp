#include "pathfinder_fs.hpp"

#include <functional>
#include <iostream>
#include <utility>
#include <fcntl.h>      // For open
#include <sys/stat.h>   // For mode constants
#include <unistd.h>     // For close, read, write if needed

using namespace std;
namespace fs = boost::filesystem;

namespace pathfinder {


// A basic file truncation function suitable for this test.
// Status Truncate(Env* env, const std::string& filename, uint64_t length) {
//   std::unique_ptr<SequentialFile> orig_file;
//   const EnvOptions options;
//   Status s = env->NewSequentialFile(filename, &orig_file, options);
//   if (!s.ok()) {
//     fprintf(stderr, "Cannot open file %s for truncation: %s\n",
//             filename.c_str(), s.ToString().c_str());
//     return s;
//   }

//   std::unique_ptr<char[]> scratch(new char[length]);
//   ROCKSDB_NAMESPACE::Slice result;
//   s = orig_file->Read(length, &result, scratch.get());
// #ifdef OS_WIN
//   orig_file.reset();
// #endif
//   if (s.ok()) {
//     std::string tmp_name = GetDirName(filename) + "/truncate.tmp";
//     std::unique_ptr<WritableFile> tmp_file;
//     s = env->NewWritableFile(tmp_name, &tmp_file, options);
//     if (s.ok()) {
//       s = tmp_file->Append(result);
//       if (s.ok()) {
//         s = env->RenameFile(tmp_name, filename);
//       } else {
//         fprintf(stderr, "Cannot rename file %s to %s: %s\n", tmp_name.c_str(),
//                 filename.c_str(), s.ToString().c_str());
//         env->DeleteFile(tmp_name);
//       }
//     }
//   }
//   if (!s.ok()) {
//     fprintf(stderr, "Cannot truncate file %s: %s\n", filename.c_str(),
//             s.ToString().c_str());
//   }

//   return s;
// }

// Status FileState::DropUnsyncedData(Env* env) const {
//   ssize_t sync_pos = pos_at_last_sync_ == -1 ? 0 : pos_at_last_sync_;
//   return Truncate(env, filename_, sync_pos);
// }

// Status FileState::DropRandomUnsyncedData(Env* env, Random* rand) const {
//   ssize_t sync_pos = pos_at_last_sync_ == -1 ? 0 : pos_at_last_sync_;
//   assert(pos_ >= sync_pos);
//   int range = static_cast<int>(pos_ - sync_pos);
//   uint64_t truncated_size =
//       static_cast<uint64_t>(sync_pos) + rand->Uniform(range);
//   return Truncate(env, filename_, truncated_size);
// }

// Status TestDirectory::Fsync() {
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }
//   env_->SyncDir(dirname_);
//   return dir_->Fsync();
// }

// Status TestDirectory::Close() {
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }
//   return dir_->Close();
// }

// TestRandomAccessFile::TestRandomAccessFile(
//     std::unique_ptr<RandomAccessFile>&& target, FaultInjectionTestEnv* env)
//     : target_(std::move(target)), env_(env) {
//   assert(target_);
//   assert(env_);
// }

// Status TestRandomAccessFile::Read(uint64_t offset, size_t n, Slice* result,
//                                   char* scratch) const {
//   assert(env_);
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }

//   assert(target_);
//   return target_->Read(offset, n, result, scratch);
// }

// Status TestRandomAccessFile::Prefetch(uint64_t offset, size_t n) {
//   assert(env_);
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }

//   assert(target_);
//   return target_->Prefetch(offset, n);
// }

// Status TestRandomAccessFile::MultiRead(ReadRequest* reqs, size_t num_reqs) {
//   assert(env_);
//   if (!env_->IsFilesystemActive()) {
//     const Status s = env_->GetError();

//     assert(reqs);
//     for (size_t i = 0; i < num_reqs; + +i) {
//       reqs[i].status = s;
//     }

//     return s;
//   }

//   assert(target_);
//   return target_->MultiRead(reqs, num_reqs);
// }

// TestWritableFile::TestWritableFile(const std::string& fname,
//                                    std::unique_ptr<WritableFile>&& f,
//                                    FaultInjectionTestEnv* env)
//     : state_(fname),
//       target_(std::move(f)),
//       writable_file_opened_(true),
//       env_(env) {
//   assert(target_ != nullptr);
//   state_.pos_ = 0;
// }

// TestWritableFile::~TestWritableFile() {
//   if (writable_file_opened_) {
//     Close().PermitUncheckedError();
//   }
// }

// Status TestWritableFile::Append(const Slice& data) {
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }
//   Status s = target_->Append(data);
//   if (s.ok()) {
//     state_.pos_ += data.size();
//     env_->WritableFileAppended(state_);
//   }
//   return s;
// }

// Status TestWritableFile::Close() {
//   writable_file_opened_ = false;
//   Status s = target_->Close();
//   if (s.ok()) {
//     env_->WritableFileClosed(state_);
//   }
//   return s;
// }

// Status TestWritableFile::Flush() {
//   Status s = target_->Flush();
//   if (s.ok() && env_->IsFilesystemActive()) {
//     state_.pos_at_last_flush_ = state_.pos_;
//   }
//   return s;
// }

// Status TestWritableFile::Sync() {
//   if (!env_->IsFilesystemActive()) {
//     return Status::IOError("FaultInjectionTestEnv: not active");
//   }
//   // No need to actual sync.
//   state_.pos_at_last_sync_ = state_.pos_;
//   env_->WritableFileSynced(state_);
//   return Status::OK();
// }

// TestRandomRWFile::TestRandomRWFile(const std::string& /*fname*/,
//                                    std::unique_ptr<RandomRWFile>&& f,
//                                    FaultInjectionTestEnv* env)
//     : target_(std::move(f)), file_opened_(true), env_(env) {
//   assert(target_ != nullptr);
// }

// TestRandomRWFile::~TestRandomRWFile() {
//   if (file_opened_) {
//     Close().PermitUncheckedError();
//   }
// }

// Status TestRandomRWFile::Write(uint64_t offset, const Slice& data) {
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }
//   return target_->Write(offset, data);
// }

// Status TestRandomRWFile::Read(uint64_t offset, size_t n, Slice* result,
//                               char* scratch) const {
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }
//   return target_->Read(offset, n, result, scratch);
// }

// Status TestRandomRWFile::Close() {
//   file_opened_ = false;
//   return target_->Close();
// }

// Status TestRandomRWFile::Flush() {
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }
//   return target_->Flush();
// }

// Status TestRandomRWFile::Sync() {
//   if (!env_->IsFilesystemActive()) {
//     return env_->GetError();
//   }
//   return target_->Sync();
// }

// Status FaultInjectionTestEnv::NewDirectory(const std::string& name,
//                                            std::unique_ptr<Directory>* result) {
//   std::unique_ptr<Directory> r;
//   Status s = target()->NewDirectory(name, &r);
//   assert(s.ok());
//   if (!s.ok()) {
//     return s;
//   }
//   result->reset(new TestDirectory(this, TrimDirname(name), r.release()));
//   return Status::OK();
// }

// Status FaultInjectionTestEnv::NewWritableFile(
//     const std::string& fname, std::unique_ptr<WritableFile>* result,
//     const EnvOptions& soptions) {
//   if (!IsFilesystemActive()) {
//     return GetError();
//   }
//   // Not allow overwriting files
//   Status s = target()->FileExists(fname);
//   if (s.ok()) {
//     return Status::Corruption("File already exists.");
//   } else if (!s.IsNotFound()) {
//     assert(s.IsIOError());
//     return s;
//   }
//   s = target()->NewWritableFile(fname, result, soptions);
//   if (s.ok()) {
//     result->reset(new TestWritableFile(fname, std::move(*result), this));
//     // WritableFileWriter* file is opened
//     // again then it will be truncated - so forget our saved state.
//     UntrackFile(fname);
//     MutexLock l(&mutex_);
//     open_managed_files_.insert(fname);
//     auto dir_and_name = GetDirAndName(fname);
//     auto& list = dir_to_new_files_since_last_sync_[dir_and_name.first];
//     list.insert(dir_and_name.second);
//   }
//   return s;
// }

// Status FaultInjectionTestEnv::ReopenWritableFile(
//     const std::string& fname, std::unique_ptr<WritableFile>* result,
//     const EnvOptions& soptions) {
//   if (!IsFilesystemActive()) {
//     return GetError();
//   }

//   bool exists;
//   Status s, exists_s = target()->FileExists(fname);
//   if (exists_s.IsNotFound()) {
//     exists = false;
//   } else if (exists_s.ok()) {
//     exists = true;
//   } else {
//     s = exists_s;
//     exists = false;
//   }

//   if (s.ok()) {
//     s = target()->ReopenWritableFile(fname, result, soptions);
//   }

//   // Only track files we created. Files created outside of this
//   // `FaultInjectionTestEnv` are not eligible for tracking/data dropping
//   // (for example, they may contain data a previous db_stress run expects to
//   // be recovered). This could be extended to track/drop data appended once
//   // the file is under `FaultInjectionTestEnv`'s control.
//   if (s.ok()) {
//     bool should_track;
//     {
//       MutexLock l(&mutex_);
//       if (db_file_state_.find(fname) != db_file_state_.end()) {
//         // It was written by this `Env` earlier.
//         assert(exists);
//         should_track = true;
//       } else if (!exists) {
//         // It was created by this `Env` just now.
//         should_track = true;
//         open_managed_files_.insert(fname);
//         auto dir_and_name = GetDirAndName(fname);
//         auto& list = dir_to_new_files_since_last_sync_[dir_and_name.first];
//         list.insert(dir_and_name.second);
//       } else {
//         should_track = false;
//       }
//     }
//     if (should_track) {
//       result->reset(new TestWritableFile(fname, std::move(*result), this));
//     }
//   }
//   return s;
// }

// Status FaultInjectionTestEnv::NewRandomRWFile(
//     const std::string& fname, std::unique_ptr<RandomRWFile>* result,
//     const EnvOptions& soptions) {
//   if (!IsFilesystemActive()) {
//     return GetError();
//   }
//   Status s = target()->NewRandomRWFile(fname, result, soptions);
//   if (s.ok()) {
//     result->reset(new TestRandomRWFile(fname, std::move(*result), this));
//     // WritableFileWriter* file is opened
//     // again then it will be truncated - so forget our saved state.
//     UntrackFile(fname);
//     MutexLock l(&mutex_);
//     open_managed_files_.insert(fname);
//     auto dir_and_name = GetDirAndName(fname);
//     auto& list = dir_to_new_files_since_last_sync_[dir_and_name.first];
//     list.insert(dir_and_name.second);
//   }
//   return s;
// }

// Status FaultInjectionTestEnv::NewRandomAccessFile(
//     const std::string& fname, std::unique_ptr<RandomAccessFile>* result,
//     const EnvOptions& soptions) {
//   if (!IsFilesystemActive()) {
//     return GetError();
//   }

//   assert(target());
//   const Status s = target()->NewRandomAccessFile(fname, result, soptions);
//   if (!s.ok()) {
//     return s;
//   }

//   assert(result);
//   result->reset(new TestRandomAccessFile(std::move(*result), this));

//   return Status::OK();
// }

// Status FaultInjectionTestEnv::DeleteFile(const std::string& f) {
//   if (!IsFilesystemActive()) {
//     return GetError();
//   }
//   Status s = EnvWrapper::DeleteFile(f);
//   if (s.ok()) {
//     UntrackFile(f);
//   }
//   return s;
// }

int pathfinder_fs::rename_file(const std::string& s,
                                         const std::string& t) {
  if (!is_filesystem_active()) {
    return -1;
  }
  int rt = rename(s.c_str(), t.c_str());
  if (rt) {
      cerr << "File rename failed!" << endl;
      cerr << string(strerror(errno)) << endl;
      exit(EXIT_FAILURE);
  }

  if (db_file_state_.find(s) != db_file_state_.end()) {
    db_file_state_[t] = db_file_state_[s];
    db_file_state_.erase(s);
  }
  auto sdn = get_dir_and_name(s);
  auto tdn = get_dir_and_name(t);
  
  if (dir_to_new_files_since_last_sync_[sdn.first].erase(sdn.second) != 0) {
    auto& tlist = dir_to_new_files_since_last_sync_[tdn.first];
    // assert(tlist.find(tdn.second) == tlist.end());
    tlist.insert(tdn.second);
  }

  return rt;
}

int pathfinder_fs::create_file(const std::string& f, int mode) {
  if (!is_filesystem_active()) {
    return -1;
  }
  int fd = creat(f.c_str(), mode);
  if (fd == -1) {
    cerr << "File creation failed!" << endl;
    cerr << string(strerror(errno)) << endl;
    exit(EXIT_FAILURE);
  }

  open_managed_files_.insert(f);
  auto dir_and_name = get_dir_and_name(f);

  if (dir_and_name.second == "001119.dbtmp") {
    cout << "STOP" << endl;
  }
  auto& list = dir_to_new_files_since_last_sync_[dir_and_name.first];
  list.insert(dir_and_name.second);
  return fd;
}

int pathfinder_fs::open_file(const std::string& f, int flag, int mode) {
  if (!is_filesystem_active()) {
    return -1;
  }
  int fd = open(f.c_str(), flag, mode);
  if (fd == -1) {
    cerr << "File open failed!" << endl;
    cerr << string(strerror(errno)) << endl;
    exit(EXIT_FAILURE);
  }
  // if the flag includes O_CREAT, then the file is created
  auto dir_and_name = get_dir_and_name(f);
  if (dir_and_name.second == "001119.dbtmp") {
    cout << "STOP" << endl;
  }
  if (flag & O_CREAT) {
    open_managed_files_.insert(f);
    auto dir_and_name = get_dir_and_name(f);
    auto& list = dir_to_new_files_since_last_sync_[dir_and_name.first];
    list.insert(dir_and_name.second);
  }
  return fd;

}

int pathfinder_fs::unlink_file(const std::string& f) {
  if (!is_filesystem_active()) {
    return -1;
  }
  int rt = unlink(f.c_str());
  if (rt) {
      cerr << "File unlink failed!" << endl;
      cerr << string(strerror(errno)) << endl;
      exit(EXIT_FAILURE);
  }
  untrack_file(f);
  return rt;
}

int pathfinder_fs::fsync_file_or_dir(const std::string& f, int fd) {
  if (!is_filesystem_active()) {
    return -1;
  }
  // if f is a dir
  if (fs::is_directory(f)) {
    if (dir_to_new_files_since_last_sync_.find(f) != dir_to_new_files_since_last_sync_.end()) {
      dir_to_new_files_since_last_sync_.erase(f);
    }
  }
  int rt = fsync(fd);
    if (rt) {
        cerr << "File fsync failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
  return rt;
}

// Status FaultInjectionTestEnv::LinkFile(const std::string& s,
//                                        const std::string& t) {
//   if (!IsFilesystemActive()) {
//     return GetError();
//   }
//   Status ret = EnvWrapper::LinkFile(s, t);

//   if (ret.ok()) {
//     MutexLock l(&mutex_);
//     if (db_file_state_.find(s) != db_file_state_.end()) {
//       db_file_state_[t] = db_file_state_[s];
//     }

//     auto sdn = GetDirAndName(s);
//     auto tdn = GetDirAndName(t);
//     if (dir_to_new_files_since_last_sync_[sdn.first].find(sdn.second) !=
//         dir_to_new_files_since_last_sync_[sdn.first].end()) {
//       auto& tlist = dir_to_new_files_since_last_sync_[tdn.first];
//       assert(tlist.find(tdn.second) == tlist.end());
//       tlist.insert(tdn.second);
//     }
//   }

//   return ret;
// }

// void FaultInjectionTestEnv::WritableFileClosed(const FileState& state) {
//   MutexLock l(&mutex_);
//   if (open_managed_files_.find(state.filename_) != open_managed_files_.end()) {
//     db_file_state_[state.filename_] = state;
//     open_managed_files_.erase(state.filename_);
//   }
// }

// void FaultInjectionTestEnv::WritableFileSynced(const FileState& state) {
//   MutexLock l(&mutex_);
//   if (open_managed_files_.find(state.filename_) != open_managed_files_.end()) {
//     if (db_file_state_.find(state.filename_) == db_file_state_.end()) {
//       db_file_state_.insert(std::make_pair(state.filename_, state));
//     } else {
//       db_file_state_[state.filename_] = state;
//     }
//   }
// }

// void FaultInjectionTestEnv::WritableFileAppended(const FileState& state) {
//   MutexLock l(&mutex_);
//   if (open_managed_files_.find(state.filename_) != open_managed_files_.end()) {
//     if (db_file_state_.find(state.filename_) == db_file_state_.end()) {
//       db_file_state_.insert(std::make_pair(state.filename_, state));
//     } else {
//       db_file_state_[state.filename_] = state;
//     }
//   }
// }

// // For every file that is not fully synced, make a call to `func` with
// // FileState of the file as the parameter.
// Status FaultInjectionTestEnv::DropFileData(
//     std::function<Status(Env*, FileState)> func) {
//   Status s;
//   MutexLock l(&mutex_);
//   for (std::map<std::string, FileState>::const_iterator it =
//            db_file_state_.begin();
//        s.ok() && it != db_file_state_.end(); ++it) {
//     const FileState& state = it->second;
//     if (!state.IsFullySynced()) {
//       s = func(target(), state);
//     }
//   }
//   return s;
// }

// Status FaultInjectionTestEnv::DropUnsyncedFileData() {
//   return DropFileData([&](Env* env, const FileState& state) {
//     return state.DropUnsyncedData(env);
//   });
// }

// Status FaultInjectionTestEnv::DropRandomUnsyncedFileData(Random* rnd) {
//   return DropFileData([&](Env* env, const FileState& state) {
//     return state.DropRandomUnsyncedData(env, rnd);
//   });
// }

// Status FaultInjectionTestEnv::DeleteFilesCreatedAfterLastDirSync() {
//   // Because DeleteFile access this container make a copy to avoid deadlock
//   std::map<std::string, std::set<std::string>> map_copy;
//   {
//     MutexLock l(&mutex_);
//     map_copy.insert(dir_to_new_files_since_last_sync_.begin(),
//                     dir_to_new_files_since_last_sync_.end());
//   }

//   for (auto& pair : map_copy) {
//     for (std::string name : pair.second) {
//       Status s = DeleteFile(pair.first + "/" + name);
//       if (!s.ok()) {
//         return s;
//       }
//     }
//   }
//   return Status::OK();
// }
// void FaultInjectionTestEnv::ResetState() {
//   MutexLock l(&mutex_);
//   db_file_state_.clear();
//   dir_to_new_files_since_last_sync_.clear();
//   SetFilesystemActiveNoLock(true);
// }

void pathfinder_fs::untrack_file(const std::string& f) {
  auto dir_and_name = get_dir_and_name(f);
  dir_to_new_files_since_last_sync_[dir_and_name.first].erase(
      dir_and_name.second);
  db_file_state_.erase(f);
  open_managed_files_.erase(f);
}

void pathfinder_fs::reset_state() {
  db_file_state_.clear();
  dir_to_new_files_since_last_sync_.clear();
  open_managed_files_.clear();
  filesystem_active_ = true;
}
  
}  // namespace pathfinder
