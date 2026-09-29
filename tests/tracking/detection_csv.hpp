#pragma once

// Tracking 테스트용 CSV 로더.
// 폴더 안의 detections.csv / frames.csv를 첫 줄의 열 이름으로 읽는다.
//   detections.csv: frame_id, x, y, width, height, confidence, (class_id), (gt_id)
//                   gt_id가 있으면 정답 박스로도 읽는다 (1 이상: 정답 물체 번호, -1: 오검출)
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

    int col(std::initializer_list<const char*> names) const
    {
        for (const char* name : names)
            for (size_t i = 0; i < header.size(); ++i)
                if (header[i] == name)
                    return static_cast<int>(i);
        return -1;
    }
};

inline std::string clean(std::string s, bool lower)
{
    s.erase(std::remove_if(s.begin(), s.end(),
        [](unsigned char c) { return c == '\r' || c == '"'; }), s.end());
    const auto b = s.find_first_not_of(" \t");
    const auto e = s.find_last_not_of(" \t");
    s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
    if (lower)
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline std::vector<std::string> split(const std::string& line, bool lower)
{
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ','))
        out.push_back(clean(cell, lower));
    return out;
}

inline bool read_csv(const std::string& path, Csv& csv)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    if (!std::getline(in, line))
        return false;
    csv.header = split(line, true);
    while (std::getline(in, line))
    {
        if (clean(line, false).empty() || line[0] == '#')
            continue;
        csv.rows.push_back(split(line, false));
    }
    return true;
}

// 이 헤더가 있는 폴더(tests/tracking)의 절대 경로
inline std::string tracking_test_dir()
{
    const std::string file = __FILE__;
    const auto pos = file.find_last_of('/');
    return pos == std::string::npos ? std::string(".") : file.substr(0, pos);
}

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
    bool has_gt = false;
};

// 성공하면 빈 문자열, 실패하면 오류 메시지를 반환한다.
inline std::string load_sequence(const std::string& dir, Sequence& seq)
{
    Csv det;
    if (!read_csv(dir + "/detections.csv", det))
        return "detections.csv를 열 수 없습니다: " + dir;

    const int c_frame = det.col({"frame_id", "frame"});
    const int c_x = det.col({"x", "left"});
    const int c_y = det.col({"y", "top"});
    const int c_w = det.col({"width", "w"});
    const int c_h = det.col({"height", "h"});
    const int c_score = det.col({"confidence", "score", "conf"});
    const int c_class = det.col({"class_id", "class", "cls"});
    const int c_gt = det.col({"gt_id"});
    seq.has_gt = c_gt >= 0;

    if (c_frame < 0 || c_x < 0 || c_y < 0 || c_w < 0 || c_h < 0 || c_score < 0)
    {
        std::string msg = "detections.csv 열 이름을 찾지 못했습니다. 현재 헤더:";
        for (const auto& h : det.header)
            msg += " [" + h + "]";
        return msg;
    }

    for (const auto& row : det.rows)
    {
        Detection d;
        d.x = std::stof(row.at(c_x));
        d.y = std::stof(row.at(c_y));
        d.width = std::stof(row.at(c_w));
        d.height = std::stof(row.at(c_h));
        d.confidence = std::stof(row.at(c_score));
        if (c_class >= 0 && c_class < static_cast<int>(row.size()))
            d.class_id = std::stoi(row[c_class]);
        const int frame = std::stoi(row.at(c_frame));
        seq.by_frame[frame].push_back(d);

        if (seq.has_gt)
        {
            const int gt_id = std::stoi(row.at(c_gt));
            if (gt_id > 0)
                seq.gt_by_frame[frame].push_back({gt_id, d.x, d.y, d.width, d.height});
        }
    }

    Csv fr;
    if (read_csv(dir + "/frames.csv", fr) && fr.col({"frame_id", "frame"}) >= 0)
    {
        const int c = fr.col({"frame_id", "frame"});
        for (const auto& row : fr.rows)
            seq.frames.push_back(std::stoi(row.at(c)));
    }
    else
    {
        for (const auto& kv : seq.by_frame)
            seq.frames.push_back(kv.first);
    }

    if (seq.frames.empty())
        return "프레임이 없습니다: " + dir;
    return "";
}

}  // namespace test_csv
