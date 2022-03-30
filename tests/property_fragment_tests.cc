#include "grape/fragment/property_fragment.h"
#include "grape/util.h"

#include "examples/snb_ldbc/ic6.h"

#include <string>
#include <iostream>
#include <fstream>

using grape::PropertyType;

void preprocessLine(char* line) {
  size_t len = strlen(line);
  while (len >= 0) {
    if (line[len] != '\0' && line[len] != '\n' &&
        line[len] != '\r' && line[len] != ' ' && line[len] != '\t') {
      break;
    } else {
      --len;
    }
  }
  line[len + 1] = '\0';
}

int main(int argc, char** argv) {
  std::string prefix = argv[1];
  std::string suffix = "_0_0.csv";
  std::string query_path = argv[2];
  std::string output_path = argv[3];

  grape::Schema schema;
  std::vector<std::pair<std::string, std::string>> vertex_files;
  std::vector<std::tuple<std::string, std::string, std::string, std::string>> edge_files;
  schema.add_vertex_label("PLACE", {
                                       PropertyType::kString, // name
                                       PropertyType::kString, // url
                                       PropertyType::kString, // type
                                   });
  vertex_files.emplace_back("PLACE", prefix + "/static/place" + suffix);
  schema.add_vertex_label("PERSON", {
                                        PropertyType::kString,  // firstName
                                        PropertyType::kString,  // lastName
                                        PropertyType::kString,  // gender
                                        PropertyType::kString,  // birthday
                                        PropertyType::kDate, // PropertyType::kString,  // creationDate
                                        PropertyType::kString,  // locationIP
                                        PropertyType::kString,  // browserUsed
                                    });
  vertex_files.emplace_back("PERSON", prefix + "/dynamic/person" + suffix);
  schema.add_vertex_label("COMMENT", {
                                         PropertyType::kDate, // PropertyType::kString,  // creationDate
                                         PropertyType::kString, // locationIP
                                         PropertyType::kString, // browserUsed
                                         PropertyType::kString, // content
                                         PropertyType::kInt32, // length
                                     });
  vertex_files.emplace_back("COMMENT", prefix + "/dynamic/comment" + suffix);
  schema.add_vertex_label("POST", {
                                      PropertyType::kString, // imageFile
                                      PropertyType::kDate, // PropertyType::kString,  // creationDate
                                      PropertyType::kString, // locationIP
                                      PropertyType::kString, // browserUsed
                                      PropertyType::kString, // language
                                      PropertyType::kString, // content
                                      PropertyType::kInt32,  // length
                                  });
  vertex_files.emplace_back("POST", prefix + "/dynamic/post" + suffix);
  schema.add_vertex_label("FORUM", {
                                       PropertyType::kString, // title
                                       PropertyType::kDate, // PropertyType::kString,  // creationDate
                                   });
  vertex_files.emplace_back("FORUM", prefix + "/dynamic/forum" + suffix);
  schema.add_vertex_label("ORGANISATION", {
                                              PropertyType::kString, // type
                                              PropertyType::kString, // name
                                              PropertyType::kString, // url
                                          });
  vertex_files.emplace_back("ORGANISATION", prefix + "/static/organisation" + suffix);
  schema.add_vertex_label("TAGCLASS", {
                                          PropertyType::kString, // name
                                          PropertyType::kString, // url
                                      });
  vertex_files.emplace_back("TAGCLASS", prefix + "/static/tagclass" + suffix);
  schema.add_vertex_label("TAG", {
                                     PropertyType::kString, // name
                                     PropertyType::kString, // url
                                 });
  vertex_files.emplace_back("TAG", prefix + "/static/tag" + suffix);
  schema.add_edge_label("COMMENT", "PERSON", "HASCREATOR", {});

  schema.add_vertex_label("EMAILADDRESS", {});
  schema.add_vertex_label("LANGUAGE", {});

  edge_files.emplace_back("COMMENT", "PERSON", "HASCREATOR", prefix + "/dynamic/comment_hasCreator_person" + suffix);
  schema.add_edge_label("POST", "PERSON", "HASCREATOR", {});
  edge_files.emplace_back("POST", "PERSON", "HASCREATOR", prefix + "/dynamic/post_hasCreator_person" + suffix);
  schema.add_edge_label("COMMENT", "TAG", "HASTAG", {});
  edge_files.emplace_back("COMMENT", "TAG", "HASTAG", prefix + "/dynamic/comment_hasTag_tag" + suffix);
  schema.add_edge_label("FORUM", "TAG", "HASTAG", {});
  edge_files.emplace_back("FORUM", "TAG", "HASTAG", prefix + "/dynamic/forum_hasTag_tag" + suffix);
  schema.add_edge_label("POST", "TAG", "HASTAG", {});
  edge_files.emplace_back("POST", "TAG", "HASTAG", prefix + "/dynamic/post_hasTag_tag" + suffix);
  schema.add_edge_label("COMMENT", "COMMENT", "REPLYOF", {});
  edge_files.emplace_back("COMMENT", "COMMENT", "REPLYOF", prefix + "/dynamic/comment_replyOf_comment" + suffix);
  schema.add_edge_label("COMMENT", "POST", "REPLYOF", {});
  edge_files.emplace_back("COMMENT", "POST", "REPLYOF", prefix + "/dynamic/comment_replyOf_post" + suffix);
  schema.add_edge_label("FORUM", "POST", "CONTAINEROF", {});
  edge_files.emplace_back("FORUM", "POST", "CONTAINEROF", prefix + "/dynamic/forum_containerOf_post" + suffix);
  schema.add_edge_label("FORUM", "PERSON", "HASMEMBER", {
                                                            PropertyType::kDate // PropertyType::kString // joinDate
                                                        });
  edge_files.emplace_back("FORUM", "PERSON", "HASMEMBER", prefix + "/dynamic/forum_hasMember_person" + suffix);
  schema.add_edge_label("FORUM", "PERSON", "HASMODERATOR", {});
  edge_files.emplace_back("FORUM", "PERSON", "HASMODERATOR", prefix + "/dynamic/forum_hasModerator_person" + suffix);
  schema.add_edge_label("PERSON", "TAG", "HASINTEREST", {});
  edge_files.emplace_back("PERSON", "TAG", "HASINTEREST", prefix + "/dynamic/person_hasInterest_tag" + suffix);
  schema.add_edge_label("COMMENT", "PLACE", "ISLOCATEDIN", {});
  edge_files.emplace_back("COMMENT", "PLACE", "ISLOCATEDIN", prefix + "/dynamic/comment_isLocatedIn_place" + suffix);
  schema.add_edge_label("PERSON", "PLACE", "ISLOCATEDIN", {});
  edge_files.emplace_back("PERSON", "PLACE", "ISLOCATEDIN", prefix + "/dynamic/person_isLocatedIn_place" + suffix);
  schema.add_edge_label("POST", "PLACE", "ISLOCATEDIN", {});
  edge_files.emplace_back("POST", "PLACE", "ISLOCATEDIN", prefix + "/dynamic/post_isLocatedIn_place" + suffix);
  schema.add_edge_label("ORGANISATION", "PLACE", "ISLOCATEDIN", {});
  edge_files.emplace_back("ORGANISATION", "PLACE", "ISLOCATEDIN", prefix + "/static/organisation_isLocatedIn_place" + suffix);
  schema.add_edge_label("PERSON", "PERSON", "KNOWS", {
                                                         PropertyType::kDate // PropertyType::kString // creationDate
                                                     });
  edge_files.emplace_back("PERSON", "PERSON", "KNOWS", prefix + "/dynamic/person_knows_person" + suffix);
  schema.add_edge_label("PERSON", "COMMENT", "LIKES", {
                                                          PropertyType::kDate // PropertyType::kString // creationDate
                                                      });
  edge_files.emplace_back("PERSON", "COMMENT", "LIKES", prefix + "/dynamic/person_likes_comment" + suffix);
  schema.add_edge_label("PERSON", "POST", "LIKES", {
                                                          PropertyType::kDate // PropertyType::kString // creationDate
                                                      });
  edge_files.emplace_back("PERSON", "POST", "LIKES", prefix + "/dynamic/person_likes_post" + suffix);
  schema.add_edge_label("PERSON", "ORGANISATION", "WORKAT", {
                                                                PropertyType::kInt32 // workFrom
                                                            });
  edge_files.emplace_back("PERSON", "ORGANISATION", "WORKAT", prefix + "/dynamic/person_workAt_organisation" + suffix);
  schema.add_edge_label("PLACE", "PLACE", "ISPARTOF", {});
  edge_files.emplace_back("PLACE", "PLACE", "ISPARTOF", prefix + "/static/place_isPartOf_place" + suffix);
  schema.add_edge_label("TAG", "TAGCLASS", "HASTYPE", {});
  edge_files.emplace_back("TAG", "TAGCLASS", "HASTYPE", prefix + "/static/tag_hasType_tagclass" + suffix);
  schema.add_edge_label("TAGCLASS", "TAGCLASS", "ISSUBCLASSOF", {});
  edge_files.emplace_back("TAGCLASS", "TAGCLASS", "ISSUBCLASSOF", prefix + "/static/tagclass_isSubclassOf_tagclass" + suffix);

  schema.add_edge_label("PERSON", "EMAILADDRESS", "EMAIL", {});
  edge_files.emplace_back("PERSON", "EMAILADDRESS", "EMAIL", prefix + "/dynamic/person_email_emailaddress" + suffix);
  schema.add_edge_label("PERSON", "LANGUAGE", "SPEAKS", {});
  edge_files.emplace_back("PERSON", "LANGUAGE", "SPEAKS", prefix + "/dynamic/person_speaks_language" + suffix);
  schema.add_edge_label("PERSON", "ORGANISATION", "STUDYAT", {});
  edge_files.emplace_back("PERSON", "ORGANISATION", "STUDYAT", prefix + "/dynamic/person_studyAt_organisation" + suffix);

  grape::PropertyFragment fragment;
  fragment.Init(schema, vertex_files, edge_files);

  grape::IC6 ic6(fragment);

  std::ofstream ostrm(output_path, std::ios::binary);
  FILE* fin = fopen(query_path.c_str(), "r");
  char line_buf[4096];
#if 0
  while (fgets(line_buf, 4096, fin) != NULL) {
    preprocessLine(line_buf);
    if (line_buf[0] == 'i' && line_buf[1] == 'c' && line_buf[2] == '6') {
      ic6.Query(line_buf, ostrm);
    }
  }
#else
  std::vector<std::pair<int64_t, std::string>> params;
  std::vector<nonstd::string_view> splits;
  while (fgets(line_buf, 4096, fin) != NULL) {
    preprocessLine(line_buf);
    grape::split(line_buf, splits, ',');
    params.emplace_back(std::stol(splits[1].to_string()), splits[2].to_string());
  }

  const int iteration = 10000;
  int params_num = params.size();
  double t0 = -grape::GetCurrentTime();
  for (int i = 0; i < iteration; ++i) {
    auto& pair = params[i % params_num];
    ic6.Query(pair.first, pair.second, ostrm);
  }
  t0 += grape::GetCurrentTime();
  LOG(INFO) << t0 / static_cast<double>(iteration) << " (s)";
#endif

  ostrm.flush();
  ostrm.close();

  return 0;
}
