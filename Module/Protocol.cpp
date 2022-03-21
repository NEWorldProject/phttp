/*
* Copyright (c) 2022 DWVoid and Infinideastudio Team
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include "kls/phttp/Error.h"
#include "kls/phttp/Protocol.h"
#include "kls/coroutine/Operation.h"

using namespace kls::io;
using namespace kls::essential;
using namespace kls::coroutine;

namespace kls::phttp {
    ClientEndpoint::ClientEndpoint(std::unique_ptr<Endpoint> endpoint) : m_down{false}, m_receive{},
                                                                         m_endpoint{std::move(endpoint)} {
        m_receive = [this]() -> ValueAsync<> {
            std::unordered_map<int32_t, Message> staging{};
            while (!m_down) {
                auto block = co_await m_endpoint->get();
                const auto id = block.id();
                auto stage_it = staging.find(id);
                if (stage_it == staging.end()) stage_it = staging.insert_or_assign(id, Message{}).first;
                stage_it->second.blocks[stage_it->second.stage++] = std::move(block);
                if (stage_it->second.stage == 3) {
                    {
                        std::lock_guard lk{m_sync};
                        auto promise_it = m_promises.find(id);
                        if (m_promises.end() != promise_it) throw InconsistentState{};
                        promise_it->second->set(std::move(stage_it->second));
                        m_promises.erase(promise_it);
                    }
                    staging.erase(stage_it);
                }
            }
        }();
    }

    ValueAsync<Response> ClientEndpoint::exec(Request &&request) {
        auto memory_resource = std::pmr::get_default_resource();
        auto line_block = request.line.pack(memory_resource);
        auto header_block = request.headers.pack(memory_resource);
        auto body_block = std::move(request.body);
        auto&&[assigned_id, receive_future] = [this]() {
            std::lock_guard lk{m_sync};
            for (;;) {
                auto id = m_top_id++;
                if (m_promises.find(id) == m_promises.end()) continue;
                return std::pair<int32_t, ValueFuture<Message>>{
                        std::piecewise_construct, std::forward_as_tuple(id),
                        std::forward_as_tuple([this, id](auto promise) { m_promises.insert({id, promise}); })
                };
            }
        }();
        line_block.set_id(assigned_id);
        header_block.set_id(assigned_id);
        body_block.set_id(assigned_id);
        try {
            MutexLock lk = co_await m_mutex.scoped_lock_async();
            co_await m_endpoint->put(std::move(line_block));
            co_await m_endpoint->put(std::move(header_block));
            co_await m_endpoint->put(std::move(body_block));
        }
        catch (...) {
            std::lock_guard lk{m_sync};
            m_promises.erase(assigned_id);
            throw;
        }
        Message response_message = co_await receive_future;
        co_return Response{
                .line = ResponseLine::unpack(response_message.blocks[0].content(), memory_resource),
                .headers = Headers::unpack(response_message.blocks[1].content(), memory_resource),
                .body = std::move(response_message.blocks[2])
        };
    }

    ValueAsync<> ClientEndpoint::close() {
        m_down = true;
        return awaits(m_endpoint->close(), m_receive);
    }

    coroutine::ValueAsync<void> ServerEndpoint::run() {
        thread::SpinLock lock{};
        std::unordered_map<int32_t, Message> staging{};
        std::unordered_map<int32_t, ValueAsync<>> processing{};
        auto processor = [&, this](int32_t id, Message msg) -> ValueAsync<void> {
            co_await Redispatch{};
            auto memory_resource = std::pmr::get_default_resource();
            try {
                Response response = co_await m_trivial(Request{
                        .line = RequestLine::unpack(msg.blocks[0].content(), memory_resource),
                        .headers = Headers::unpack(msg.blocks[1].content(), memory_resource),
                        .body = std::move(msg.blocks[2])
                }, m_data);
                auto line_block = response.line.pack(memory_resource);
                auto header_block = response.headers.pack(memory_resource);
                auto body_block = std::move(response.body);
                MutexLock lk = co_await m_mutex.scoped_lock_async();
                co_await m_endpoint->put(std::move(line_block));
                co_await m_endpoint->put(std::move(header_block));
                co_await m_endpoint->put(std::move(body_block));
            }
            catch (std::exception &e) { puts(e.what()); }
            catch (...) {}
            std::lock_guard lk{lock};
            processing.erase(id);
        };
        while (!m_down) {
            auto block = co_await m_endpoint->get();
            const auto id = block.id();
            auto stage_it = staging.find(id);
            if (stage_it == staging.end()) stage_it = staging.insert_or_assign(id, Message{}).first;
            stage_it->second.blocks[stage_it->second.stage++] = std::move(block);
            if (stage_it->second.stage == 3) {
                {
                    std::lock_guard lk{lock};
                    processing.insert({id, processor(id, std::move(stage_it->second))});
                }
                staging.erase(stage_it);
            }
        }
        {
            lock.lock();
            auto final = std::move(processing);
            lock.unlock();
            for (auto&&[k, v]: final) co_await std::move(v);
        }
    }

    coroutine::ValueAsync<> ServerEndpoint::close() {
        m_down = true;
        co_await m_endpoint->close();
    }
}
