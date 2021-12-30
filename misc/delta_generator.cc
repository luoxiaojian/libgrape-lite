#include <stdio.h>

#include <string>
#include <random>

enum class RecordType {
  kBase,
  kAdd,
  kRemove,
  kUpdate,
};

class RdType {
 public:
  RdType(int base, int add, int remove, int update)
      : add_(base),
        remove_(base + add),
        update_(base + add + remove),
        total_(base + add + remove + update),
        gen_(std::random_device{}()) {}

  RecordType gen() {
    int rd = gen_() % total_;
    if (rd < add_) {
      return RecordType::kBase;
    } else if (rd < remove_) {
      return RecordType::kAdd;
    } else if (rd < update_) {
      return RecordType::kRemove;
    } else {
      return RecordType::kUpdate;
    }
  }

 private:
  int add_;
  int remove_;
  int update_;
  int total_;

  std::mt19937 gen_;
};

void update_func(const std::string& from, std::string& to) {
  to = from;
  int loc = to.length() - 1;
  bool ret = (to[loc] == '\n');
  while (loc >= 0 && (to[loc] != ' ' && to[loc] != '\t')) {
    --loc;
  }
  to.resize(loc + 1);
  to.push_back('0');
  if (ret) {
    to.push_back('\n');
  }
}

int main(int argc, char** argv) {
  if (argc < 5) {
    printf("usage: ./delta_generator <efile> <output-prefix> <base> <add> [remove] [update]\n");
    return 0;
  }
  std::string efile_name = argv[1];
  std::string output_prefix = argv[2];
  int base = atoi(argv[3]);
  int add = atoi(argv[4]);
  int remove = 0;
  if (argc >= 6) {
    remove = atoi(argv[5]);
  }
  int update = 0;
  if (argc >= 7) {
    update = atoi(argv[6]);
  }

  RdType gen(base, add, remove, update);
  FILE* fin = fopen(efile_name.c_str(), "r");
  std::string mutable_base_fname = output_prefix + ".mutable_base";
  std::string mutable_delta_fname = output_prefix + ".mutable_delta";
  std::string immutable_fname = output_prefix + ".immutable";
  FILE* mutable_base = fopen(mutable_base_fname.c_str(), "wb");
  FILE* mutable_delta = fopen(mutable_delta_fname.c_str(), "wb");
  FILE* immutable = fopen(immutable_fname.c_str(), "wb");

  const size_t LINE_SIZE = 65535;
  char buff[LINE_SIZE];
  while (fgets(buff, LINE_SIZE, fin)) {
    RecordType type = gen.gen();
    if (type == RecordType::kBase) {
      fputs(buff, mutable_base);
      fputs(buff, immutable);
    } else if (type == RecordType::kAdd) {
      fputc('a', mutable_delta);
      fputc(' ', mutable_delta);
      fputs(buff, mutable_delta);
      fputs(buff, immutable);
    } else if (type == RecordType::kRemove) {
      fputs(buff, mutable_base);
      fputc('d', mutable_delta);
      fputc(' ', mutable_delta);
      fputs(buff, mutable_delta);
    } else {
      fputs(buff, immutable);
      fputc('u', mutable_delta);
      fputc(' ', mutable_delta);
      fputs(buff, mutable_delta);
      std::string from(buff);
      std::string to;
      update_func(from, to);
      fputs(to.c_str(), mutable_base);
    }
  }

  fflush(mutable_base);
  fflush(mutable_delta);
  fflush(immutable);
  fclose(mutable_base);
  fclose(mutable_delta);
  fclose(immutable);

  return 0;
}