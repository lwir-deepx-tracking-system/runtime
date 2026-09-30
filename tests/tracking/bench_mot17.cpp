#include "tracking/bytetrack_tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "tracking_metrics.hpp"

// MOT17 공개 데이터로 ByteTrack 실행 (분석 도구, ctest에는 등록하지 않음)
//
// MOT17Labels.zip(영상 없는 버전)의 det.txt(공개 검출)를 ByteTrack에 넣고
//   1. 시퀀스별 검출 점수 분포 (ByteTrack 임계값과 맞는 검출인지 확인)
//   2. 결과 파일 <out>/<시퀀스>.txt  (MOT 형식: frame,id,x,y,w,h,conf,-1,-1,-1 → TrackEval 입력)
//   3. 빠른 지표 (IDSW, 헛출력, 누락, IDF1, MOTA)  ※ 공식 수치는 TrackEval로 확인
// 를 출력한다.
//
// 빠른 지표의 정답 처리는 MOTChallenge(TrackEval) 규칙을 따른다.
//   - 평가 대상 정답: gt.txt 7열(consider) = 1 이고 8열(class) = 1(보행자)
//   - 방해 클래스(2 탈것 위 사람, 7 정지한 사람, 8 방해물, 12 반사)와 IoU 0.5 이상으로
//     짝지어진 출력은 헛출력으로 세지 않고 제외한다
//
// 사용법
//   bench_mot17 <MOT17 폴더> [--det FRCNN|SDP|DPM] [--out 결과폴더] [--config yaml] [시퀀스 이름 필터...]
//     <MOT17 폴더> : train/ 이 들어 있는 폴더 또는 train 폴더 자체
//     --det        : 사용할 공개 검출기 (기본 FRCNN)
//     --out        : 결과 파일 폴더 (기본: 저장하지 않음)
//     --config     : ByteTrack YAML (기본: 공식 기본값). frame_rate는 항상 seqinfo.ini 값을 쓴다
//   예) bench_mot17 datasets/MOT17 --out results/bytetrack/data 02 04

namespace fs = std::filesystem;

namespace
{

struct SeqInfo
{
    std::string name;
    int frame_rate = 30;
    int length = 0;
};

struct GtRow
{
    int id;
    float x, y, w, h;
    int consider;
    int cls;
};

// seqinfo.ini에서 key=value 읽기
SeqInfo read_seqinfo(const fs::path& file)
{
    SeqInfo s;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "name")
            s.name = value;
        else if (key == "frameRate")
            s.frame_rate = std::stoi(value);
        else if (key == "seqLength")
            s.length = std::stoi(value);
    }
    return s;
}

// 쉼표로 구분된 숫자 한 줄
std::vector<double> split_numbers(const std::string& line)
{
    std::vector<double> v;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ','))
        if (!cell.empty() && cell != "\r")
            v.push_back(std::stod(cell));
    return v;
}

// det.txt: frame, -1, x, y, w, h, score, ... (프레임 순서로 정렬돼 있지 않다)
std::map<int, std::vector<Detection>> read_detections(const fs::path& file, std::vector<float>& scores)
{
    std::map<int, std::vector<Detection>> by_frame;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
    {
        const auto v = split_numbers(line);
        if (v.size() < 7)
            continue;
        Detection d;
        d.x = static_cast<float>(v[2]);
        d.y = static_cast<float>(v[3]);
        d.width = static_cast<float>(v[4]);
        d.height = static_cast<float>(v[5]);
        d.confidence = static_cast<float>(v[6]);
        d.class_id = 0;
        by_frame[static_cast<int>(v[0])].push_back(d);
        scores.push_back(d.confidence);
    }
    return by_frame;
}

// gt.txt: frame, id, x, y, w, h, consider, class, visibility
std::map<int, std::vector<GtRow>> read_gt(const fs::path& file)
{
    std::map<int, std::vector<GtRow>> by_frame;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
    {
        const auto v = split_numbers(line);
        if (v.size() < 8)
            continue;
        by_frame[static_cast<int>(v[0])].push_back(
            {static_cast<int>(v[1]), static_cast<float>(v[2]), static_cast<float>(v[3]),
             static_cast<float>(v[4]), static_cast<float>(v[5]), static_cast<int>(v[6]),
             static_cast<int>(v[7])});
    }
    return by_frame;
}

bool is_distractor(int cls) { return cls == 2 || cls == 7 || cls == 8 || cls == 12; }

// 방해 클래스 정답과 짝지어진 출력을 빼고, 평가 대상 정답만 남긴다 (TrackEval 전처리와 같은 규칙)
void preprocess(const std::vector<GtRow>& gt_all, std::vector<Track>& tracks,
                std::vector<test_csv::GtBox>& gt_eval, int& removed)
{
    gt_eval.clear();
    for (const auto& g : gt_all)
        if (g.consider != 0 && g.cls == 1)
            gt_eval.push_back({g.id, g.x, g.y, g.w, g.h});

    if (gt_all.empty() || tracks.empty())
        return;
    const size_t n = std::max(gt_all.size(), tracks.size());
    constexpr double kInvalid = 1e6;
    std::vector<std::vector<double>> cost(n, std::vector<double>(n, kInvalid));
    for (size_t i = 0; i < gt_all.size(); ++i)
        for (size_t j = 0; j < tracks.size(); ++j)
        {
            const auto& g = gt_all[i];
            const auto& t = tracks[j];
            const float v = tracking_metrics::iou_tlwh(g.x, g.y, g.w, g.h, t.x, t.y, t.width, t.height);
            if (v >= 0.5f)
                cost[i][j] = 1.0 - v;
        }
    const auto assign = tracking_metrics::min_cost_assignment(cost);
    std::vector<char> drop(tracks.size(), 0);
    for (size_t i = 0; i < gt_all.size(); ++i)
    {
        const int j = assign[i];
        if (j >= 0 && static_cast<size_t>(j) < tracks.size() && cost[i][j] < kInvalid &&
            is_distractor(gt_all[i].cls))
            drop[j] = 1;
    }
    std::vector<Track> kept;
    for (size_t j = 0; j < tracks.size(); ++j)
    {
        if (drop[j])
            ++removed;
        else
            kept.push_back(tracks[j]);
    }
    tracks.swap(kept);
}

void print_score_distribution(const std::vector<float>& scores, const ByteTrackConfig& c)
{
    int band[4] = {0, 0, 0, 0};
    float lo = scores.empty() ? 0.0f : scores[0], hi = lo;
    for (float s : scores)
    {
        lo = std::min(lo, s);
        hi = std::max(hi, s);
        if (s <= c.low_thresh)
            ++band[0];
        else if (s < c.track_thresh)
            ++band[1];
        else if (s < c.new_track_thresh)
            ++band[2];
        else
            ++band[3];
    }
    const double n = scores.empty() ? 1.0 : static_cast<double>(scores.size());
    std::printf("  검출 %zu개, 점수 범위 %.3f ~ %.3f\n", scores.size(), lo, hi);
    std::printf("    버림(<=%.2f) %5.1f%%   low(2차 매칭) %5.1f%%   high %5.1f%%   high+새 ID 가능(>=%.2f) %5.1f%%\n",
                c.low_thresh, 100.0 * band[0] / n, 100.0 * band[1] / n, 100.0 * band[2] / n,
                c.new_track_thresh, 100.0 * band[3] / n);
    if (lo < 0.0f || hi > 1.0f)
        std::printf("    ! 점수가 0~1 범위가 아닙니다 (DPM). ByteTrack 임계값과 의미가 맞지 않습니다\n");
    else if (band[1] == 0)
        std::printf("    ! low 구간 검출이 없어 ByteTrack의 2차 매칭(핵심 기능)이 쓰이지 않습니다\n");
}

struct Totals
{
    long gt = 0, pred = 0, matched = 0, idsw = 0, idtp = 0, frames = 0;
    double ms = 0.0;
};

}  // namespace


int main(int argc, char** argv)
{
    std::string root_arg, det_name = "FRCNN", out_dir, config_path;
    std::vector<std::string> filters;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 >= argc)
            {
                std::cerr << a << " 뒤에 값이 필요합니다\n";
                std::exit(2);
            }
            dst = argv[++i];
        };
        if (a == "--det")
            next(det_name);
        else if (a == "--out")
            next(out_dir);
        else if (a == "--config")
            next(config_path);
        else if (a == "-h" || a == "--help")
        {
            std::cout << "사용법: bench_mot17 <MOT17 폴더> [--det FRCNN|SDP|DPM] [--out 결과폴더] "
                         "[--config yaml] [시퀀스 필터...]\n";
            return 0;
        }
        else if (root_arg.empty())
            root_arg = a;
        else
            filters.push_back(a);
    }
    if (root_arg.empty())
    {
        std::cerr << "MOT17 폴더를 지정하세요. 예) bench_mot17 datasets/MOT17\n";
        return 2;
    }

    fs::path root = root_arg;
    if (fs::is_directory(root / "train"))
        root /= "train";

    ByteTrackConfig base;
    try
    {
        if (!config_path.empty())
            base = load_bytetrack_config(config_path);
    }
    catch (const std::exception& e)
    {
        std::cerr << "설정 오류: " << e.what() << "\n";
        return 2;
    }

    std::vector<fs::path> seqs;
    if (fs::is_directory(root))
        for (const auto& e : fs::directory_iterator(root))
        {
            const std::string name = e.path().filename().string();
            if (!e.is_directory() || name.size() < det_name.size() + 1 ||
                name.compare(name.size() - det_name.size() - 1, std::string::npos, "-" + det_name) != 0)
                continue;
            bool keep = filters.empty();
            for (const auto& f : filters)
                keep = keep || name.find(f) != std::string::npos;
            if (keep && fs::exists(e.path() / "det" / "det.txt"))
                seqs.push_back(e.path());
        }
    std::sort(seqs.begin(), seqs.end());
    if (seqs.empty())
    {
        std::cerr << "시퀀스가 없습니다: " << root << " (*-" << det_name << "/det/det.txt)\n";
        return 1;
    }
    if (!out_dir.empty())
        fs::create_directories(out_dir);

    std::printf("\n[MOT17 %s, 시퀀스 %zu개]  ByteTrack track %.2f / low %.2f / new %.2f / match %.2f / buffer %d\n",
                det_name.c_str(), seqs.size(), base.track_thresh, base.low_thresh, base.new_track_thresh,
                base.match_thresh, base.track_buffer);

    struct Row
    {
        std::string name;
        tracking_metrics::Result m;
        int removed;
        double ms;
        int frames;
        bool has_gt;
    };
    std::vector<Row> rows;
    Totals total;

    for (const auto& dir : seqs)
    {
        SeqInfo info = read_seqinfo(dir / "seqinfo.ini");
        if (info.name.empty())
            info.name = dir.filename().string();
        std::vector<float> scores;
        auto dets = read_detections(dir / "det" / "det.txt", scores);
        if (info.length <= 0)
            info.length = dets.empty() ? 0 : dets.rbegin()->first;
        const bool has_gt = fs::exists(dir / "gt" / "gt.txt");
        const auto gt = has_gt ? read_gt(dir / "gt" / "gt.txt") : std::map<int, std::vector<GtRow>>{};

        std::printf("\n%s  (%d프레임, %dfps)\n", info.name.c_str(), info.length, info.frame_rate);
        print_score_distribution(scores, base);

        ByteTrackConfig cfg = base;
        cfg.frame_rate = info.frame_rate;
        ByteTrackTracker tracker("mot17", cfg);
        tracking_metrics::Accumulator metrics;
        std::ofstream out;
        if (!out_dir.empty())
            out.open(fs::path(out_dir) / (info.name + ".txt"));

        int removed = 0;
        double ms = 0.0;
        static const std::vector<Detection> kEmpty;
        static const std::vector<GtRow> kNoGt;
        // 검출이 없는 프레임도 빠짐없이 넣어야 Track이 나이를 먹는다
        for (int f = 1; f <= info.length; ++f)
        {
            const auto it = dets.find(f);
            const auto& in = it != dets.end() ? it->second : kEmpty;
            const auto t0 = std::chrono::steady_clock::now();
            auto tracks = tracker.track(in);
            ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

            if (out.is_open())
                for (const auto& t : tracks)
                {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), "%d,%d,%.2f,%.2f,%.2f,%.2f,%.3f,-1,-1,-1\n", f,
                                  t.track_id, t.x, t.y, t.width, t.height, t.confidence);
                    out << buf;
                }
            if (has_gt)
            {
                const auto g = gt.find(f);
                std::vector<test_csv::GtBox> gt_eval;
                preprocess(g != gt.end() ? g->second : kNoGt, tracks, gt_eval, removed);
                metrics.add_frame(f, gt_eval, tracks);
            }
        }
        rows.push_back({info.name, metrics.finish(), removed, ms, info.length, has_gt});
        const auto& m = rows.back().m;
        total.frames += info.length;
        total.ms += ms;
        if (has_gt)
        {
            total.gt += m.gt_count;
            total.pred += m.pred_count;
            total.matched += m.matched;
            total.idsw += m.idsw;
            total.idtp += m.idtp;
        }
    }

    std::printf("\n[빠른 지표]  IoU 0.5. 공식 수치(HOTA 포함)는 TrackEval로 확인\n");
    // 한글은 printf 폭 계산에서 3바이트로 세어져 줄이 어긋나므로 머리글은 공백을 직접 맞춘다
    std::printf("  sequence              GT    pred    IDSW  헛출력    누락    IDF1    MOTA  ms/frame\n");
    std::printf("  %s\n", std::string(84, '-').c_str());
    for (const auto& r : rows)
    {
        const auto& m = r.m;
        if (!r.has_gt)
        {
            std::printf("  %-16s  (gt.txt 없음, 결과 파일만 저장)  %32.3f\n", r.name.c_str(), r.ms / r.frames);
            continue;
        }
        const int fp = m.pred_count - m.matched, fn = m.gt_count - m.matched;
        const double mota = m.gt_count > 0 ? 1.0 - static_cast<double>(fp + fn + m.idsw) / m.gt_count : 0.0;
        std::printf("  %-16s %7d %7d %7d %7d %7d %6.1f%% %6.1f%% %9.3f\n", r.name.c_str(), m.gt_count,
                    m.pred_count, m.idsw, fp, fn, 100.0 * m.idf1, 100.0 * mota, r.ms / r.frames);
    }
    if (total.gt > 0)
    {
        const long fp = total.pred - total.matched, fn = total.gt - total.matched;
        std::printf("  %s\n", std::string(84, '-').c_str());
        std::printf("  %-18s %7ld %7ld %7ld %7ld %7ld %6.1f%% %6.1f%% %9.3f\n", "합계", total.gt, total.pred,
                    total.idsw, fp, fn, 100.0 * 2.0 * total.idtp / (total.gt + total.pred),
                    100.0 * (1.0 - static_cast<double>(fp + fn + total.idsw) / total.gt), total.ms / total.frames);
    }
    int removed_all = 0;
    for (const auto& r : rows)
        removed_all += r.removed;
    std::printf("\n  방해 클래스(정지한 사람·반사 등)와 겹쳐 평가에서 제외한 출력: %d개\n", removed_all);
    if (!out_dir.empty())
        std::printf("  결과 파일: %s/<시퀀스>.txt\n", out_dir.c_str());
    std::printf("\n");
    return 0;
}
