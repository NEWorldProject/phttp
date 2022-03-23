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
    static constexpr auto Endian = std::endian::little;

    namespace detail {
        static void pack_phttp_string(essential::SpanWriter<Endian> &writer,std::string_view view) noexcept {
            auto size = int32_t(view.size());
            writer.put<int32_t>(size);
            std::copy(view.begin(), view.end(), writer.bytes(size).begin());
        }

        static std::string_view unpack_phttp_string(essential::SpanReader<Endian> &reader) {
            auto length = reader.get<int32_t>();
            auto span = reader.bytes(length);
            return {span.begin(), span.end()};
        }
    }

    Block RequestLine::pack(int32_t id, std::pmr::memory_resource *memory) const {
        auto block = Block(int32_t(m_verb.size() + m_version.size() + m_resource.size() + 12), id, memory);
        auto writer = essential::SpanWriter<Endian>(block.content());
        detail::pack_phttp_string(writer, m_verb);
        detail::pack_phttp_string(writer, m_version);
        detail::pack_phttp_string(writer, m_resource);
        return block;
    }

    RequestLine RequestLine::unpack(const Block& block, std::pmr::memory_resource *memory) {
        essential::SpanReader<Endian> reader{block.content()};
        auto verb = alias::string(detail::unpack_phttp_string(reader), {memory});
        auto version = alias::string(detail::unpack_phttp_string(reader), {memory});
        auto resource = alias::string(detail::unpack_phttp_string(reader), {memory});
        return {std::move(verb), std::move(version), std::move(resource)};
    }

    Block ResponseLine::pack(int32_t id, std::pmr::memory_resource *memory) const {
        auto block = Block(int32_t(8 + m_message.size()), id, memory);
        auto writer = essential::SpanWriter<Endian>(block.content());
        writer.put(m_code);
        detail::pack_phttp_string(writer, m_message);
        return block;
    }

    ResponseLine ResponseLine::unpack(const Block& block, std::pmr::memory_resource *memory) {
        essential::SpanReader<Endian> reader{block.content()};
        auto status = reader.get<int32_t>();
        auto message = alias::string{detail::unpack_phttp_string(reader), {memory}};
        return {status, message};
    }

    void Headers::set(std::string_view key, std::string_view value) {
        auto memory = m_table.get_allocator().resource();
        m_table.insert_or_assign(alias::string{key, {memory}}, alias::string{value, {memory}});
    }

    Block Headers::pack(int32_t id, std::pmr::memory_resource *memory) const {
        auto block = Block(std::accumulate(
                m_table.begin(), m_table.end(), int32_t(4),
                [](auto a, auto b) noexcept { return a + b.first.size() + b.second.size() + 8; }
        ), id, memory);
        auto writer = essential::SpanWriter<Endian>(block.content());
        writer.put<int32_t>(int32_t(m_table.size()));
        for (auto&&[k, v]: m_table) {
            detail::pack_phttp_string(writer, k);
            detail::pack_phttp_string(writer, v);
        }
        return block;
    }

    Headers Headers::unpack(const Block& block, std::pmr::memory_resource *memory) {
        Headers result{memory};
        essential::SpanReader<Endian> reader{block.content()};
        const auto count = reader.get<int32_t>();
        for (int i = 0; i < count; ++i) {
            auto key = detail::unpack_phttp_string(reader);
            auto value = detail::unpack_phttp_string(reader);
            result.set(key, value);
        }
        return result;
    }
}