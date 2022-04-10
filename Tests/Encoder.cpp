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

TEST(kls_phttp, EncodeRequestLine) {
    using namespace kls::phttp;
    auto raw = RequestLine("POST", "TEST_RESOURCE/A");
    auto packed = raw.pack(0, kls::pmr::default_resource());
    auto trip = RequestLine::unpack(packed, kls::pmr::default_resource());
    auto result = (trip.verb() == raw.verb()) && (trip.resource() == raw.resource());
    ASSERT_TRUE(result);
}

TEST(kls_phttp, EncodeResponseLine) {
    using namespace kls::phttp;
    auto raw = ResponseLine(20000, "SUCCESS");
    auto packed = raw.pack(0, kls::pmr::default_resource());
    auto trip = ResponseLine::unpack(packed, kls::pmr::default_resource());
    auto result = (trip.code() == raw.code()) && (trip.message() == raw.message());
    ASSERT_TRUE(result);
}

TEST(kls_phttp, EncodeHeaders) {
    using namespace kls::phttp;
    auto raw = Headers();
    raw.set("Test", "Headers");
    raw.set("Foo", "Bar");
    auto packed = raw.pack(0, kls::pmr::default_resource());
    auto trip = Headers::unpack(packed, kls::pmr::default_resource());
    auto result = (trip.get("Test") == raw.get("Test")) && (trip.get("Foo") == raw.get("Foo"));
    ASSERT_TRUE(result);
}
