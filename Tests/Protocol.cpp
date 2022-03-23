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

#include <string>
#include <gtest/gtest.h>
#include "kls/phttp/Protocol.h"
#include "kls/coroutine/Blocking.h"
#include "kls/coroutine/Operation.h"

using namespace kls::io;
using namespace kls::phttp;
using namespace kls::essential;
using namespace kls::coroutine;

static ValueAsync<void> ServerOnceEcho() {
    auto host = listen_tcp({Address::CreateIPv4("0.0.0.0").value(), 33080}, 128);
    co_await uses(host, [](Host &host) -> ValueAsync<> {
        auto peer = ServerEndpoint::create(co_await host.accept());
        co_await uses(peer, [](ServerEndpoint &ep)-> ValueAsync<> {
            co_await ep.run([](Request request) -> ValueAsync<Response> {
                co_return Response {
                        .line = ResponseLine(200, "OK"),
                        .headers = std::move(request.headers),
                        .body = std::move(request.body)
                };
            });
        });
    });
};

static ValueAsync<void> ClientOnce() {
    auto client = ClientEndpoint::create(co_await connect_tcp({Address::CreateIPv4("127.0.0.1").value(), 33080}));
    auto result = co_await uses(client, [](ClientEndpoint &ep) -> ValueAsync<bool> {
        auto memory = std::pmr::get_default_resource();
        auto raw = ResponseLine(20000, "OK");
        auto request = Request {
                .line = RequestLine("ECHO", "/"),
                .headers = Headers(),
                .body = raw.pack(0, memory)
        };
        auto response = co_await ep.exec(std::move(request));
        auto trip = ResponseLine::unpack(response.body, memory);
        co_return (trip.code() == raw.code()) && (trip.message() == raw.message());
    });
    if (!result) throw std::runtime_error("Transport Echo Content Check Failure");
}

TEST(kls_phttp, ProtocolEcho) {
    run_blocking([&]() -> ValueAsync<void> {
        co_await kls::coroutine::awaits(ServerOnceEcho(), ClientOnce());
    });
}