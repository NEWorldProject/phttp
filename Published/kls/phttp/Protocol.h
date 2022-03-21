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

#pragma once

#include "Message.h"
#include "kls/coroutine/Async.h"
#include "kls/coroutine/Mutex.h"
#include "kls/coroutine/Future.h"
#include <unordered_map>

namespace kls::phttp {
    class ClientEndpoint {
    public:
        explicit ClientEndpoint(std::unique_ptr<Endpoint> endpoint);
        coroutine::ValueAsync<Response> exec(Request &&request);
        coroutine::ValueAsync<> close();
    private:
        struct Message {
            int stage{0};
            Block blocks[3];
        };
        std::atomic_bool m_down;
        coroutine::Mutex m_mutex{};
        coroutine::ValueAsync<> m_receive;
        std::unique_ptr<Endpoint> m_endpoint;
        // response sync back
        int32_t m_top_id{0};
        thread::SpinLock m_sync{};
        std::unordered_map<int32_t, coroutine::ValueFuture<Message>::PromiseType *> m_promises{};
    };

    class ServerEndpoint {
        using Trivial = coroutine::ValueAsync<Response>(*)(Request &&, void *);
    public:
        explicit ServerEndpoint(std::unique_ptr<Endpoint> endpoint) : m_endpoint(std::move(endpoint)) {}
        template<class Fn>
        requires requires(Fn fn, Request request) {
            { fn(request) } -> std::same_as<coroutine::ValueAsync<Response>>;
        }
        coroutine::ValueAsync<void> run(Fn handler) {
            m_data = &handler;
            m_trivial = [](Request &&request, void *data) { return static_cast<Fn *>(data)(std::move(request)); };
            co_await run();
        }
        coroutine::ValueAsync<> close();
    private:
        struct Message {
            int stage{0};
            Block blocks[3];
        };
        void *m_data{};
        Trivial m_trivial{};
        std::atomic_bool m_down{false};
        coroutine::Mutex m_mutex{};
        std::unique_ptr<Endpoint> m_endpoint;

        coroutine::ValueAsync<void> run();
    };
}