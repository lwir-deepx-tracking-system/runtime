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
#include <set>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "config/app_config.hpp"

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
//   bench_mot17 <MOT17 폴더> [--det FRCNN|SDP|DPM] [--out 결과폴더] [--config yaml] [--gmc GMC폴더] [시퀀스 이름 필터...]
//     <MOT17 폴더> : train/ 이 들어 있는 폴더 또는 train 폴더 자체
//     --det        : 사용할 공개 검출기 (기본 FRCNN)
//     --out        : 결과 파일 폴더 (기본: 저장하지 않음)
//     --config     : Tracking 설정 YAML (기본: TrackingConfig 기본값). 두 형식 모두 읽는다
//                    - runtime.yaml / benchmark.yaml 처럼 tracking: 섹션이 있는 파일
//                    - track_threshold: ... 처럼 tracking 키만 나열한 파일 (sweep_mot17.py가 만드는 형식)
//                    모르는 키(오타)와 v2에서 없어진 frame_rate 키는 오류로 처리한다
//     track_buffer : v2 Tracker는 fps 환산을 하지 않으므로, 예전 결과와 맞추기 위해 여기서
//                    track_buffer × (seqinfo fps / 30)으로 환산해 넘긴다 (05는 14fps)
//     --gmc        : 카메라 움직임 보정(CMC). 폴더 안의 GMC-MOT17-XX.txt를 읽어 프레임마다 적용한다
//                    BoT-SORT가 영상으로 미리 계산해 공개한 파일 형식 (MIT 라이선스):
//                    줄 번호(0부터), a, b, tx, c, d, ty  (탭 구분, n번째 줄 = n프레임 → n+1프레임 변환)
//   예) bench_mot17 datasets/MOT17 --out results/bytetrack/data 02 04

namespace fs = std::filesystem;

namespace
{

struct SeqInfo
{
    std::string name;
    int frame_rate = 30;
    int length = 0;
    int width = 0;
    int height = 0;
};

// GMC-MOT17-XX.txt: 줄마다 "번호 a b tx c d ty". 반환 벡터의 i번째 = (i)프레임 → (i+1)프레임 변환
bool read_gmc(const fs::path& file, std::vector<CameraMotion>& out)
{
    std::ifstream in(file);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream ss(line);
        int idx = 0;
        CameraMotion m;
        if (ss >> idx >> m.a >> m.b >> m.tx >> m.c >> m.d >> m.ty)
            out.push_back(m);
    }
    return !out.empty();
}

// "MOT17-13-FRCNN" → "MOT17-13"
std::string base_sequence(const std::string& name)
{
    const auto pos = name.rfind('-');
    return pos == std::string::npos || name.compare(0, 5, "MOT17") != 0 || pos <= 5 ? name : name.substr(0, pos);
}

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
        else if (key == "imWidth")
            s.width = std::stoi(value);
        else if (key == "imHeight")
            s.height = std::stoi(value);
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

// ByteTrackTracker와 같은 규칙: new_track_threshold가 0이면 track_threshold + 0.1
float new_track_threshold(const TrackingConfig& c)
{
    return c.new_track_threshold > 0.0f ? c.new_track_threshold : std::min(c.track_threshold + 0.1f, 1.0f);
}

// --config 파일을 읽는다. tracking: 섹션이 있으면 그 안을, 없으면 파일 전체를 tracking 설정으로 본다.
// 오타가 조용히 무시되지 않도록 모르는 키는 오류로 처리한다.
TrackingConfig load_config(const std::string& path)
{
    const YAML::Node root = config_parser::load_yaml(path);
    const YAML::Node node = root["tracking"] ? root["tracking"] : root;
    if (!node.IsMap())
        throw std::runtime_error(path + ": tracking 설정이 key: value 형식이 아닙니다");
    static const std::set<std::string> known = {
        "track_threshold", "match_threshold", "track_buffer", "fuse_score", "lost_output_frames",
        "buffer_scale", "buffer_max_px", "buffer_small_size", "buffer_snap",
        "low_threshold", "new_track_threshold", "low_match_threshold", "unconfirmed_match_threshold"};
    static const std::set<std::string> removed = {"frame_rate"};
    for (const auto& kv : node)
    {
        const std::string key = kv.first.as<std::string>();
        if (removed.count(key))
            throw std::runtime_error(path + ": '" + key + "'는 v2 Tracker에서 설정할 수 없습니다 (고정값)");
        if (!known.count(key))
            throw std::runtime_error(path + ": 알 수 없는 키 '" + key + "'");
    }
    return config_parser::load_tracking_config(node);
}

void print_score_distribution(const std::vector<float>& scores, const TrackingConfig& c)
{
    const float low = c.low_threshold, new_thresh = new_track_threshold(c);
    int band[4] = {0, 0, 0, 0};
    float lo = scores.empty() ? 0.0f : scores[0], hi = lo;
    for (float s : scores)
    {
        lo = std::min(lo, s);
        hi = std::max(hi, s);
        if (s <= low)
            ++band[0];
        else if (s < c.track_threshold)
            ++band[1];
        else if (s < new_thresh)
            ++band[2];
        else
            ++band[3];
    }
    const double n = scores.empty() ? 1.0 : static_cast<double>(scores.size());
    std::printf("  검출 %zu개, 점수 범위 %.3f ~ %.3f\n", scores.size(), lo, hi);
    std::printf("    버림(<=%.2f) %5.1f%%   low(2차 매칭) %5.1f%%   high %5.1f%%   high+새 ID 가능(>=%.2f) %5.1f%%\n",
                low, 100.0 * band[0] / n, 100.0 * band[1] / n, 100.0 * band[2] / n,
                new_thresh, 100.0 * band[3] / n);
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
    std::string root_arg, det_name = "FRCNN", out_dir, config_path, gmc_dir;
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
        else if (a == "--gmc")
            next(gmc_dir);
        else if (a == "-h" || a == "--help")
        {
            std::cout << "사용법: bench_mot17 <MOT17 폴더> [--det FRCNN|SDP|DPM] [--out 결과폴더] "
                         "[--config yaml] [--gmc GMC폴더] [시퀀스 필터...]\n";
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

    TrackingConfig base;
    try
    {
        if (!config_path.empty())
            base = load_config(config_path);
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
                det_name.c_str(), seqs.size(), base.track_threshold, base.low_threshold, new_track_threshold(base),
                base.match_threshold, base.track_buffer);
    std::printf("  2차 매칭 %.2f / 미확정 매칭 %.2f (1 - IoU)\n", base.low_match_threshold,
                base.unconfirmed_match_threshold);
    std::printf("  fuse_score %s / lost_output_frames %d / buffer_scale %.2f (max_px %.0f, small %.0f, snap %s)\n",
                base.fuse_score ? "켬" : "끔", base.lost_output_frames, base.buffer_scale, base.buffer_max_px,
                base.buffer_small_size, base.buffer_snap ? "켬" : "끔");
    if (!gmc_dir.empty())
        std::printf("  CMC 켬: %s/GMC-<시퀀스>.txt\n", gmc_dir.c_str());

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
    std::vector<double> frame_ms;  // 모든 시퀀스의 프레임별 처리 시간 (p99·최대 계산용)

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

        // 예전 Tracker처럼 track_buffer를 30fps 기준으로 보고 시퀀스 fps에 맞춰 환산한다
        TrackingConfig cfg = base;
        cfg.track_buffer = std::max(1, static_cast<int>(info.frame_rate / 30.0 * base.track_buffer));
        ByteTrackTracker tracker(cfg);
        tracker.set_image_size(static_cast<float>(info.width), static_cast<float>(info.height));
        std::vector<CameraMotion> gmc;
        if (!gmc_dir.empty())
        {
            const fs::path file = fs::path(gmc_dir) / ("GMC-" + base_sequence(info.name) + ".txt");
            if (!read_gmc(file, gmc))
            {
                std::cerr << "GMC 파일을 읽을 수 없습니다: " << file << "\n";
                return 1;
            }
            if (static_cast<int>(gmc.size()) < info.length)
                std::printf("  ! GMC 줄 수(%zu)가 프레임 수(%d)보다 적습니다. 남은 프레임은 보정하지 않습니다\n",
                            gmc.size(), info.length);
        }
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
            // GMC 파일 f-1번째 줄 = (f-1)프레임 → f프레임 변환 (첫 줄은 항등)
            if (f >= 2 && f - 1 < static_cast<int>(gmc.size()))
                tracker.set_camera_motion(gmc[f - 1]);
            auto tracks = tracker.track(in);
            const double dt = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ms += dt;
            frame_ms.push_back(dt);

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
    if (!frame_ms.empty())
    {
        std::sort(frame_ms.begin(), frame_ms.end());
        std::printf("\n  처리 시간:  track() %zu회, 평균 %.4f ms, p99 %.4f ms, 최대 %.4f ms"
                    "  (set_camera_motion + track 구간만, 파일 읽기·지표 계산 제외)\n",
                    frame_ms.size(), total.ms / frame_ms.size(), frame_ms[frame_ms.size() * 99 / 100],
                    frame_ms.back());
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
