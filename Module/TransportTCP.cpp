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

#include <utility>
#include "kls/io/TCPUtil.h"
#include "kls/phttp/Transport.h"
#include "kls/essential/Unsafe.h"

using namespace kls::io;
using namespace kls::phttp;
using namespace kls::thread;
using namespace kls::essential;
using namespace kls::coroutine;

namespace {
    class EndpointImpl : public Endpoint {
    public:
        EndpointImpl(std::unique_ptr<SocketTCP> s, Peer p) noexcept: m_peer(std::move(p)), m_socket(std::move(s)) {}

        [[nodiscard]] Peer peer() const noexcept override { return m_peer; }

        ValueAsync<> put(Block block) override {
            (co_await write_fully(*m_socket, block.bytes())).get_result();
        }

        ValueAsync<Block> get() override {
            char buffer[8];
            (co_await read_fully(*m_socket, {buffer, 8})).get_result();
            SpanReader<std::endian::little> headReader{{buffer, 8}};
            const auto msgId = headReader.get<int32_t>();
            const auto msgLen = headReader.get<int32_t>();
            auto block = Block(msgLen, msgId, std::pmr::get_default_resource());
            (co_await read_fully(*m_socket, block.bytes())).get_result();
            co_return block;
        }

        ValueAsync<> close() override { co_await m_socket->close(); }
    private:
        Peer m_peer;
        std::unique_ptr<SocketTCP> m_socket;
    };

    class ServerImpl : public Host {
    public:
        explicit ServerImpl(std::unique_ptr<AcceptorTCP> a) noexcept: m_accept(std::move(a)) {}

        ValueAsync<std::unique_ptr<Endpoint>> accept() override {
            auto&&[peer, stream] = co_await m_accept->once();
            co_return std::make_unique<EndpointImpl>(std::move(stream), peer);
        }

        ValueAsync<> close() override { co_await m_accept->close(); }
    private:
        std::unique_ptr<AcceptorTCP> m_accept;
    };
}

namespace kls::phttp {
    [[nodiscard]] coroutine::ValueAsync<std::unique_ptr<Host>> listen_tcp(io::Peer local, int backlog) {
        co_return std::make_unique<ServerImpl>(acceptor_tcp(local.first, local.second, backlog));
    }

    [[nodiscard]] coroutine::ValueAsync<std::unique_ptr<Endpoint>> connect_tcp(io::Peer peer) {
        co_return std::make_unique<EndpointImpl>(co_await connect(peer.first, peer.second), peer);
    }
}