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

#include <string_view>
#include <memory_resource>
#include "kls/essential/STL.h"
#include "kls/essential/Unsafe.h"
#include "Transport.h"

namespace kls::phttp {
    namespace detail {
        using alias = kls::essential::AllocAliased<std::pmr::polymorphic_allocator>;
    }

    class RequestLine {
        using alias = detail::alias;
    public:
        RequestLine() = default;
        RequestLine(
                std::string_view verb, std::string_view resource,
                std::pmr::memory_resource *mem = std::pmr::get_default_resource()
        ) : m_verb{verb, {mem}}, m_version{"PHTTP/1.0", {mem}}, m_resource{resource, {mem}} {}

        [[nodiscard]] std::string_view verb() const noexcept { return {m_verb}; }
        [[nodiscard]] std::string_view version() const noexcept { return {m_version}; }
        [[nodiscard]] std::string_view resource() const noexcept { return {m_resource}; }
        [[nodiscard]] Block pack(std::pmr::memory_resource *memory);
        [[nodiscard]] static RequestLine unpack(essential::Span<> content, std::pmr::memory_resource *memory);
    private:
        RequestLine(alias::string &&verb, alias::string &&version, alias::string &&resource)
                : m_verb{std::move(verb)}, m_version{std::move(version)}, m_resource{std::move(resource)} {}

        alias::string m_verb{}, m_version{}, m_resource{};
    };

    class ResponseLine {
        using alias = detail::alias;
    public:
        ResponseLine() = default;
        ResponseLine(
                int32_t code, std::string_view message,
                std::pmr::memory_resource *memory = std::pmr::get_default_resource()
        ) : m_code{code}, m_message{message, {memory}} {}

        [[nodiscard]] int32_t code() const noexcept { return m_code; }
        [[nodiscard]] std::string_view message() const noexcept { return {m_message}; }
        [[nodiscard]] Block pack(std::pmr::memory_resource *memory);
        [[nodiscard]] static ResponseLine unpack(essential::Span<> content, std::pmr::memory_resource *memory);
    private:
        ResponseLine(int32_t code, alias::string &&message) : m_code{code}, m_message{std::move(message)} {}

        int32_t m_code{};
        alias::string m_message{};
    };

    class Headers {
        struct string_hash {
            using hash_type = std::hash<std::string_view>;
            using is_transparent = void;
            [[nodiscard]] size_t operator()(const char *str) const { return hash_type{}(str); }
            [[nodiscard]] size_t operator()(std::string_view str) const { return hash_type{}(str); }
            [[nodiscard]] size_t operator()(std::string const &str) const { return hash_type{}(str); }
        };

        using alias = detail::alias;
    public:
        explicit Headers(
                std::pmr::memory_resource *memory = std::pmr::get_default_resource()
        ) noexcept: m_table{{memory}}{}

        [[nodiscard]] std::string_view get(std::string_view key) const noexcept {
            const auto find = m_table.find(key);
            if (find == m_table.end()) return std::string_view{};
            return std::string_view{find->second};
        }

        void set(std::string_view key, std::string_view value);
        [[nodiscard]] Block pack(std::pmr::memory_resource *memory);
        [[nodiscard]] static Headers unpack(essential::Span<> content, std::pmr::memory_resource *memory);
    private:
        void set(alias::string&& key, alias::string&& value) {
            m_table.insert_or_assign(std::move(key), std::move(value));
        }

        alias::unordered_map <alias::string, alias::string, string_hash, std::equal_to<>> m_table;
    };

    struct Request {
        RequestLine line;
        Headers headers;
        Block body;
    };

    struct Response {
        ResponseLine line;
        Headers headers;
        Block body;
    };
}