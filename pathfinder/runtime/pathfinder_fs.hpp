// Adapted from RocksDB's fault injection fs. The filesystem wrapper keeps track of the state of a filesystem as of
// the last "sync". It then checks for data loss errors by purposely dropping
// file data (or entire files) not protected by a "sync".

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/filesystem.hpp>

#include "../utils/common.hpp"
#include "../utils/file_utils.hpp"

namespace pathfinder {

// class inode {
//   protected:
//     int size;
//     bool is_dir;
//     // file data, for file inode
//     std::vector<std::shared_ptr<char>> data;
//     // filename -> inode number, for dir inode
//     std::unordered_map<std::string, int> children;
  
//   public:
//     inode(bool dir) : size(0), is_dir(dir) {}
//     virtual ~inode() {}
//     virtual int get_size() { return size; }
//     virtual void set_size(int s) { size = s; }
//     virtual bool is_directory() { return is_dir; }
//     virtual void add_child(const std::string& name, int inode_num) {
//       children[name] = inode_num;
//     }
//     virtual int get_child(const std::string& name) {
//       return children[name];
//     }
//     virtual void remove_child(const std::string& name) {
//       children.erase(name);
//     }
//     virtual std::unordered_map<std::string, int> get_children() {
//       return children;
//     }
//     virtual void add_data(const std::shared_ptr<char>& d) {
//       data.push_back(d);
//     }
//     virtual std::shared_ptr<char> get_data(int i) {
//       return data[i];
//     }
    
// }

struct file_state {
  std::string filename_;
  ssize_t pos_;
  ssize_t pos_at_last_sync_;
  ssize_t pos_at_last_flush_;

  explicit file_state(const std::string& filename)
      : filename_(filename),
        pos_(-1),
        pos_at_last_sync_(-1),
        pos_at_last_flush_(-1) {}

  file_state() : pos_(-1), pos_at_last_sync_(-1), pos_at_last_flush_(-1) {}

  bool IsFullySynced() const { return pos_ <= 0 || pos_ == pos_at_last_sync_; }

  // Status DropUnsyncedData(Env* env) const;

  // Status DropRandomUnsyncedData(Env* env, Random* rand) const;
};

// Directory object represents collection of files and implements
// filesystem operations that can be executed on directories.
// class Directory {
//  public:
//   virtual ~Directory() {}
//   // Fsync directory. Can be called concurrently from multiple threads.
//   virtual Status Fsync() = 0;
//   // Close directory.
//   virtual Status Close() { return Status::NotSupported("Close"); }

//   virtual size_t GetUniqueId(char* /*id*/, size_t /*max_size*/) const {
//     return 0;
//   }

  // If you're adding methods here, remember to add them to
  // DirectoryWrapper too.
// };

// class TestRandomAccessFile : public RandomAccessFile {
//  public:
//   TestRandomAccessFile(std::unique_ptr<RandomAccessFile>&& target,
//                        FaultInjectionTestEnv* env);

//   Status Read(uint64_t offset, size_t n, Slice* result,
//               char* scratch) const override;

//   Status Prefetch(uint64_t offset, size_t n) override;

//   Status MultiRead(ReadRequest* reqs, size_t num_reqs) override;

//  private:
//   std::unique_ptr<RandomAccessFile> target_;
//   FaultInjectionTestEnv* env_;
// };

// // A wrapper around WritableFileWriter* file
// // is written to or sync'ed.
// class TestWritableFile : public WritableFile {
//  public:
//   explicit TestWritableFile(const std::string& fname,
//                             std::unique_ptr<WritableFile>&& f,
//                             FaultInjectionTestEnv* env);
//   virtual ~TestWritableFile();
//   virtual Status Append(const Slice& data) override;
//   virtual Status Append(
//       const Slice& data,
//       const DataVerificationInfo& /*verification_info*/) override {
//     return Append(data);
//   }
//   virtual Status Truncate(uint64_t size) override {
//     return target_->Truncate(size);
//   }
//   virtual Status Close() override;
//   virtual Status Flush() override;
//   virtual Status Sync() override;
//   virtual bool IsSyncThreadSafe() const override { return true; }
//   virtual Status PositionedAppend(const Slice& data,
//                                   uint64_t offset) override {
//     return target_->PositionedAppend(data, offset);
//   }
//   virtual Status PositionedAppend(
//       const Slice& data, uint64_t offset,
//       const DataVerificationInfo& /*verification_info*/) override {
//     return PositionedAppend(data, offset);
//   }
//   virtual bool use_direct_io() const override {
//     return target_->use_direct_io();
//   };

//  private:
//   FileState state_;
//   std::unique_ptr<WritableFile> target_;
//   bool writable_file_opened_;
//   FaultInjectionTestEnv* env_;
// };

// // A wrapper around WritableFileWriter* file
// // is written to or sync'ed.
// class TestRandomRWFile : public RandomRWFile {
//  public:
//   explicit TestRandomRWFile(const std::string& fname,
//                             std::unique_ptr<RandomRWFile>&& f,
//                             FaultInjectionTestEnv* env);
//   virtual ~TestRandomRWFile();
//   Status Write(uint64_t offset, const Slice& data) override;
//   Status Read(uint64_t offset, size_t n, Slice* result,
//               char* scratch) const override;
//   Status Close() override;
//   Status Flush() override;
//   Status Sync() override;
//   size_t GetRequiredBufferAlignment() const override {
//     return target_->GetRequiredBufferAlignment();
//   }
//   bool use_direct_io() const override { return target_->use_direct_io(); };

//  private:
//   std::unique_ptr<RandomRWFile> target_;
//   bool file_opened_;
//   FaultInjectionTestEnv* env_;
// };

// class TestDirectory : public Directory {
//  public:
//   explicit TestDirectory(FaultInjectionTestEnv* env, std::string dirname,
//                          Directory* dir)
//       : env_(env), dirname_(dirname), dir_(dir) {}
//   ~TestDirectory() {}

//   virtual Status Fsync() override;
//   virtual Status Close() override;

//  private:
//   FaultInjectionTestEnv* env_;
//   std::string dirname_;
//   std::unique_ptr<Directory> dir_;
// };

class pathfinder_fs {
 public:
  explicit pathfinder_fs()
      : filesystem_active_(true) {}
  virtual ~pathfinder_fs() { }

  // Status NewDirectory(const std::string& name,
  //                     std::unique_ptr<Directory>* result) override;

  // Status NewWritableFile(const std::string& fname,
  //                        std::unique_ptr<WritableFile>* result,
  //                        const EnvOptions& soptions) override;

  // Status ReopenWritableFile(const std::string& fname,
  //                           std::unique_ptr<WritableFile>* result,
  //                           const EnvOptions& soptions) override;

  // Status NewRandomRWFile(const std::string& fname,
  //                        std::unique_ptr<RandomRWFile>* result,
  //                        const EnvOptions& soptions) override;

  // Status NewRandomAccessFile(const std::string& fname,
  //                            std::unique_ptr<RandomAccessFile>* result,
  //                            const EnvOptions& soptions) override;

  // virtual Status DeleteFile(const std::string& f) override;

  int rename_file(const std::string& s,
                            const std::string& t);
  
  int create_file(const std::string& f, int mode);

  int open_file(const std::string& f, int flag, int mode);

  int unlink_file(const std::string& f);

  int fsync_file_or_dir(const std::string& f, int fd);

  // virtual Status LinkFile(const std::string& s, const std::string& t) override;

  // void WritableFileClosed(const FileState& state);

  // void WritableFileSynced(const FileState& state);

  // void WritableFileAppended(const FileState& state);

  // // For every file that is not fully synced, make a call to `func` with
  // // FileState of the file as the parameter.
  // Status DropFileData(std::function<Status(Env*, FileState)> func);

  // Status DropUnsyncedFileData();

  // Status DropRandomUnsyncedFileData(Random* rnd);

  // Status DeleteFilesCreatedAfterLastDirSync();

  // void ResetState();

  void untrack_file(const std::string& f);

  // void sync_dir(const std::string& dirname) {
  //   // MutexLock l(&mutex_);
  //   dir_to_new_files_since_last_sync_.erase(dirname);
  // }

  // Setting the filesystem to inactive is the test equivalent to simulating a
  // system reset. Setting to inactive will freeze our saved filesystem state so
  // that it will stop being recorded. It can then be reset back to the state at
  // the time of the reset.
  bool is_filesystem_active() {
    // MutexLock l(&mutex_);
    return filesystem_active_;
  }
  // void SetFilesystemActiveNoLock(bool active,
  //     Status error = Status::Corruption("Not active")) {
  //   error.PermitUncheckedError();
  //   filesystem_active_ = active;
  //   if (!active) {
  //     error_ = error;
  //   }
  //   error.PermitUncheckedError();
  // }
  // void SetFilesystemActive(bool active,
  //     Status error = Status::Corruption("Not active")) {
  //   error.PermitUncheckedError();
  //   MutexLock l(&mutex_);
  //   SetFilesystemActiveNoLock(active, error);
  //   error.PermitUncheckedError();
  // }
  void assert_no_open_file() { assert(open_managed_files_.empty()); }
  // Status GetError() { return error_; }

  void reset_state();

 private:
  // port::Mutex mutex_;
  std::map<std::string, file_state> db_file_state_;
  std::set<std::string> open_managed_files_;
  std::unordered_map<std::string, std::set<std::string>>
      dir_to_new_files_since_last_sync_;
  bool filesystem_active_;  // Record flushes, syncs, writes
  // Status error_;
};

}  // namespace pathfinder
