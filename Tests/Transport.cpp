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
#include "kls/phttp/Message.h"
#include "kls/coroutine/Blocking.h"
#include "kls/coroutine/Operation.h"

using namespace kls::io;
using namespace kls::phttp;
using namespace kls::essential;
using namespace kls::coroutine;

static ValueAsync<> ServerOnceEcho() {
    auto host = listen_tcp({Address::CreateIPv4("0.0.0.0").value(), 33080}, 128);
    co_await uses(host, [](Host &host) -> ValueAsync<> {
        auto peer = co_await host.accept();
        co_await uses(peer, [](Endpoint &ep) -> ValueAsync<> { co_await ep.put(co_await ep.get()); });
    });
};

static ValueAsync<> ClientOnce() {
    auto file = co_await connect_tcp({Address::CreateIPv4("127.0.0.1").value(), 33080});
    auto result = co_await uses(file, [](Endpoint &ep) -> ValueAsync<bool> {
        auto memory = std::pmr::get_default_resource();
        auto raw = ResponseLine(20000, "OK");
        co_await ep.put(raw.pack(0, memory));
        auto trip = ResponseLine::unpack(co_await ep.get(), memory);
        co_return (trip.code() == raw.code()) && (trip.message() == raw.message());
    });
    if (!result) throw std::runtime_error("Transport Echo Content Check Failure");
};

TEST(kls_phttp, TransportTcpEcho) {
    run_blocking([&]() -> ValueAsync<void> {
        auto server = ServerOnceEcho();
        auto client = ClientOnce();
        // For some reason c2 refused to link this
        //co_await kls::coroutine::awaits(std::move(server), std::move(client));
        co_await std::move(server), co_await std::move(client);
    });
}