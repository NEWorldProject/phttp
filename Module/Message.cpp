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

#include <numeric>
#include "kls/phttp/Message.h"

namespace kls::phttp {
    namespace detail {
        static void pack_phttp_string(
                essential::SpanWriter<std::endian::little> &writer,
                std::string_view view
        ) noexcept {
            writer.put<int32_t>(int32_t(view.size()));
            for (auto c: view) writer.put(char(c));
        }

        static alias::string unpack_phttp_string(
                essential::SpanReader<std::endian::little> &reader,
                std::pmr::memory_resource *memory
        ) {
            auto length = reader.get<int32_t>();
            auto string = alias::string(length, ' ', {memory});
            for (auto &c: string) c = reader.get<char>();
            return string;
        }
    }

    Block RequestLine::pack(std::pmr::memory_resource *memory) {
        auto block = Block(int32_t(m_verb.size() + m_version.size() + m_resource.size() + 12), memory);
        auto writer = essential::SpanWriter<std::endian::little>(block.content());
        detail::pack_phttp_string(writer, m_verb);
        detail::pack_phttp_string(writer, m_version);
        detail::pack_phttp_string(writer, m_resource);
        return block;
    }

    RequestLine RequestLine::unpack(essential::Span<> content, std::pmr::memory_resource *memory) {
        essential::SpanReader<std::endian::little> reader{content};
        auto verb = detail::unpack_phttp_string(reader, memory);
        auto version = detail::unpack_phttp_string(reader, memory);
        auto resource = detail::unpack_phttp_string(reader, memory);
        return {std::move(verb), std::move(version), std::move(resource)};
    }

    Block ResponseLine::pack(std::pmr::memory_resource *memory) {
        auto block = Block(int32_t(8 + m_message.size()), memory);
        auto writer = essential::SpanWriter<std::endian::little>(block.content());
        writer.put(m_code);
        detail::pack_phttp_string(writer, m_message);
        return block;
    }

    ResponseLine ResponseLine::unpack(essential::Span<> content, std::pmr::memory_resource *memory) {
        essential::SpanReader<std::endian::little> reader{content};
        auto status = reader.get<int32_t>();
        auto message = detail::unpack_phttp_string(reader, memory);
        return {status, std::move(message)};
    }

    void Headers::set(std::string_view key, std::string_view value) {
        auto memory = m_table.get_allocator().resource();
        m_table.insert_or_assign(alias::string{key, {memory}}, alias::string{value, {memory}});
    }

    Block Headers::pack(std::pmr::memory_resource *memory) {
        auto block = Block(std::accumulate(
                m_table.begin(), m_table.end(), int32_t(4),
                [](auto a, auto b) noexcept { return a + b.first.size() + b.second.size(); }
        ), memory);
        auto writer = essential::SpanWriter<std::endian::little>(block.content());
        for (auto&&[k, v]: m_table) {
            detail::pack_phttp_string(writer, k);
            detail::pack_phttp_string(writer, v);
        }
        return block;
    }

    Headers Headers::unpack(essential::Span<> content, std::pmr::memory_resource *memory) {
        Headers result{memory};
        essential::SpanReader<std::endian::little> reader{content};
        const auto count = reader.get<int32_t>();
        for (int i = 0; i < count; ++i) {
            auto key = detail::unpack_phttp_string(reader, memory);
            auto value = detail::unpack_phttp_string(reader, memory);
            result.set(std::move(key), std::move(value));
        }
        return result;
    }
}