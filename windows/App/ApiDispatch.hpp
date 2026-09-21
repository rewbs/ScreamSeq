#pragma once
#include "../Api/PipeServer.hpp"
#include "../Api/SessionAdapter.hpp"
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace ScreamSeq {
// Only control/API threads touch this queue. It is never read by the audio thread.
class ApiDispatch {
	struct Job {
		Api::Json request, response;
		std::mutex mutex;
		std::condition_variable changed;
		bool begun = false, done = false, cancelled = false;
	};
	HWND window_;
	Api::SessionAdapter adapter_;
	std::mutex mutex_;
	std::deque<std::shared_ptr<Job>> queue_;
	bool closed_ = false;
	Api::PipeServer server_;
	Api::Json dispatch(const Api::Json &request) {
		auto job = std::make_shared<Job>(); job->request = request;
		{
			std::lock_guard lock(mutex_);
			if(closed_ || queue_.size() >= 16) return Api::errorResponse(request.value("id", Api::Json()), -32002, "Session busy or closing");
			queue_.push_back(job);
		}
		std::unique_lock lock(job->mutex);
		if(!PostMessageW(window_, message, 0, 0)) {
			job->cancelled = true;
			return Api::errorResponse(request.value("id", Api::Json()), -32002, "Control window unavailable");
		}
		if(!job->changed.wait_for(lock, std::chrono::seconds(5), [&]{return job->done || job->cancelled;})) {
			job->cancelled = !job->begun;
			return Api::errorResponse(request.value("id", Api::Json()), job->begun ? -32003 : -32002,
				job->begun ? "Request began but reply timed out; outcome uncertain, inspect before retry" : "Queued request cancelled before execution");
		}
		if(job->cancelled) return Api::errorResponse(request.value("id", Api::Json()), -32002, "Session closed before execution");
		return job->response;
	}
public:
	static constexpr UINT message = WM_APP + 41;
	ApiDispatch(HWND window, Api::SessionHost &host)
		: window_(window), adapter_(host),
		server_(L"\\\\.\\pipe\\ScreamSeq.Api." + std::to_wstring(GetCurrentProcessId()), [this](const Api::Json &q){return dispatch(q);}) {
		server_.start();
	}
	~ApiDispatch() { close(); }
	void drain() {
		std::deque<std::shared_ptr<Job>> jobs;
		{ std::lock_guard lock(mutex_); jobs.swap(queue_); }
		for(auto &job : jobs) {
			{ std::lock_guard lock(job->mutex); if(job->cancelled) continue; job->begun = true; }
			auto reply = adapter_.handle(job->request);
			{ std::lock_guard lock(job->mutex); job->response = std::move(reply); job->done = true; }
			job->changed.notify_one();
		}
	}
	void close() noexcept {
		{
			std::lock_guard lock(mutex_); closed_ = true;
			for(auto &job : queue_) {
				{ std::lock_guard jobLock(job->mutex); job->cancelled = true; }
				job->changed.notify_one();
			}
			queue_.clear();
		}
		server_.stop();
	}
};
}
