#include <stdio.h>

#include <string>

int main(int argc, char** argv) {
  std::string input = argv[1];
  std::string output_prefix = argv[2];
  size_t line_num = atoll(argv[3]);
  int chunk_num = atoi(argv[4]);
  int chunk = (line_num + chunk_num - 1) / chunk_num;
  int cur_line_no = chunk;
  int cur_chunk_id = -1;
  const size_t LINE_SIZE = 65535;
  char buff[LINE_SIZE];

  FILE* fin = fopen(input.c_str(), "r");
  FILE* fout = NULL;
  while (fgets(buff, LINE_SIZE, fin)) {
    if (cur_line_no == chunk) {
      cur_line_no = 0;
      ++cur_chunk_id;
      std::string output = output_prefix + ".part_" + std::to_string(cur_chunk_id);
      if (fout != NULL) {
        fflush(fout);
        fclose(fout);
      }
      fout = fopen(output.c_str(), "wb");
    }
    fputs(buff, fout);
    ++cur_line_no;
  }
  if (fout != NULL) {
    fflush(fout);
    fclose(fout);
  }

  return 0;
}
