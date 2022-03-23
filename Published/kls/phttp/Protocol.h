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

namespace kls::phttp {
    struct ChannelClosed: std::exception {
        [[nodiscard]] const char *what() const noexcept override;
    };

    struct ClientEndpoint: public PmrBase {
        virtual coroutine::ValueAsync<Response> exec(Request request) = 0;
        virtual coroutine::ValueAsync<> close() = 0;
        static std::unique_ptr<ClientEndpoint> create(std::unique_ptr<Endpoint> ep);
    };

    class ServerEndpoint: public PmrBase {
    public:
        template<class Fn>
        requires requires(Fn fn, Request request) {
            { fn(std::move(request)) } -> std::same_as<coroutine::ValueAsync<Response>>;
        }
        coroutine::ValueAsync<void> run(Fn handler) {
            m_data = &handler;
            m_trivial = [](Request &&request, void *data) { return (*static_cast<Fn *>(data))(std::move(request)); };
            co_await run();
        }
        virtual coroutine::ValueAsync<> close() = 0;
        static std::unique_ptr<ServerEndpoint> create(std::unique_ptr<Endpoint> ep);
    protected:
        using Trivial = coroutine::ValueAsync<Response>(*)(Request &&, void *);
        void *m_data{};
        Trivial m_trivial{};
        virtual coroutine::ValueAsync<void> run() = 0;
    };
}