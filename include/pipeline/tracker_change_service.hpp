#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "app/pipeline_requests.hpp"

class TrackerManager;

// 트래커 변경 요청과 완료 응답을 소유하며 생성·교체를 단일 작업 스레드에서 실행한다.
class TrackerChangeService
{
    struct Request
    {
        int                          id;
        std::optional<TrackerPolicyUpdate> policy;
        std::promise<PipelineResult> completion;
    };
    TrackerManager         &manager_;
    std::function<void()>   before_commit_;
    std::mutex              mutex_;
    std::condition_variable wake_;
    std::optional<Request>  pending_;
    bool                    started_ = false, closed_ = false, busy_ = false;
    std::thread             worker_;
    // 생성 실패는 요청 결과로 변환하고 기존 활성 트래커를 유지한다.
    void run();

  public:
    // manager와 콜백이 참조하는 객체는 서비스보다 오래 살아야 한다.
    explicit TrackerChangeService(TrackerManager &manager, std::function<void()> before_commit = {});
    ~TrackerChangeService();
    // 요청 수신 경로를 열기 전에 작업 스레드를 시작한다; 재시작은 지원하지 않는다.
    void start();
    // 접수와 완료를 분리하며 동시에 한 건만 허용한다.
    PipelineChangeTicket request_change(std::uint32_t id);
    PipelineChangeTicket request_policy_change(const TrackerPolicyUpdate& policy);
    // 신규·미실행 요청은 NotReady로 종료하고 이미 실행 중인 요청은 완료시킨다.
    void close();
    // close 후 실행 중인 생성·교체가 끝날 때까지 기다린다.
    void join();
};
