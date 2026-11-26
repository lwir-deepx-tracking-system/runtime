#pragma once

// Tracking 테스트용 CSV 로더.
// 폴더 안의 detections.csv / frames.csv를 첫 줄의 열 이름으로 읽는다.
//   detections.csv: frame_id, x, y, width, height, confidence, (class_id), (gt_id)
//                   gt_id가 있으면 정답 박스로도 읽는다 (1 이상: 정답 물체 번호, -1: 오검출)
//                   (gt_x, gt_y, gt_width, gt_height)가 있으면 정답 박스는 그 값을 쓴다
//   frames.csv    : frame_id   (검출이 없는 프레임도 포함한 전체 순서)

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "common/detection.hpp"

namespace test_csv
{

struct Csv
{
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;

    int col(std::initializer_list<const char*> names) const;
};

std::string clean(std::string s, bool lower);

std::vector<std::string> split(const std::string& line, bool lower);

bool read_csv(const std::string& path, Csv& csv);

// 이 헤더가 있는 폴더(tests/tracking)의 절대 경로
std::string tracking_test_dir();

// 정답 박스 (IDSW·IDF1 계산용). 좌표는 Detection과 같은 TLWH.
struct GtBox
{
    int id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct Sequence
{
    std::vector<int> frames;                        // 호출 순서
    std::map<int, std::vector<Detection>> by_frame; // 없는 프레임은 빈 목록
    std::map<int, std::vector<GtBox>> gt_by_frame;  // gt_id 열이 있을 때만 채워짐
    std::map<int, std::vector<GtBox>> fp_by_frame;  // gt_id -1 (오검출) 박스
    bool has_gt = false;
};

// 성공하면 빈 문자열, 실패하면 오류 메시지를 반환한다.
std::string load_sequence(const std::string& dir, Sequence& seq);

}  // namespace test_csv
