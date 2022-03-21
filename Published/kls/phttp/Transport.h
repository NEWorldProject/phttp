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

#include "kls/io/IP.h"
#include "kls/coroutine/Async.h"
#include "kls/essential/Memory.h"
#include "kls/essential/Unsafe.h"

namespace kls::phttp {
    class Block {
        [[nodiscard]] auto header() const noexcept {
            return essential::Access < std::endian::little > {{m_v.get(), 8}};
        }
    public:
        Block() noexcept: m_v(nullptr) {}
        Block(int32_t content_length, std::pmr::memory_resource *resource) :
                m_v(kls::pmr::make_unique<char[]>(resource, content_length + 8)) {
            header().put<int32_t>(4, content_length);
        }
        Block(int32_t content_length, int32_t message_id, std::pmr::memory_resource *resource) :
                m_v(kls::pmr::make_unique<char[]>(resource, content_length + 8)) {
            auto h = header();
            h.put<int32_t>(0, message_id);
            h.put<int32_t>(4, content_length);
        }
        Block(Block&&) noexcept = default;
        Block& operator=(Block&&) noexcept = default;
        void set_id(int32_t value) noexcept { header().put<int32_t>(0, value); }
        [[nodiscard]] int32_t id() const noexcept { return header().get<int32_t>(0); }
        [[nodiscard]] int32_t size() const noexcept { return header().get<int32_t>(4); }
        [[nodiscard]] essential::Span<> bytes() const noexcept { return {m_v.get(), size() + 8}; }
        [[nodiscard]] essential::Span<> content() const noexcept { return {m_v.get() + 8, size()}; }
    private:
        pmr::unique_ptr<char[]> m_v;
    };

    struct Endpoint : PmrBase {
        [[nodiscard]] virtual io::Peer peer() const noexcept = 0;
        [[nodiscard]] virtual coroutine::ValueAsync<> put(Block) = 0;
        [[nodiscard]] virtual coroutine::ValueAsync <Block> get() = 0;
        [[nodiscard]] virtual coroutine::ValueAsync<> close() = 0;
    };

    struct Host : PmrBase {
        [[nodiscard]] virtual coroutine::ValueAsync <std::unique_ptr<Endpoint>> accept() = 0;
        [[nodiscard]] virtual coroutine::ValueAsync<> close() = 0;
    };

    [[nodiscard]] coroutine::ValueAsync <std::unique_ptr<Endpoint>> connect_tcp(io::Peer peer);
    [[nodiscard]] coroutine::ValueAsync <std::unique_ptr<Host>> listen_tcp(io::Peer local, int backlog);
}
