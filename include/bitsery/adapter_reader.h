//MIT License
//
//Copyright (c) 2017 Mindaugas Vinkelis
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

#ifndef BITSERY_ADAPTER_READER_H
#define BITSERY_ADAPTER_READER_H

#include "details/adapter_common.h"
#include <algorithm>
#include <cstring>

namespace bitsery {

    template <typename TReader>
    class AdapterReaderBitPackingWrapper;

    template<typename InputAdapter, typename Config, typename Context=void>
    struct AdapterReader: public details::AdapterAndContext<InputAdapter, Config, Context> {

        using details::AdapterAndContext<InputAdapter, Config, Context>::AdapterAndContext;

        static constexpr bool BitPackingEnabled = false;

        template<size_t SIZE, typename T>
        void readBytes(T& v) {
            static_assert(std::is_integral<T>(), "");
            static_assert(sizeof(T) == SIZE, "");
            directRead(&v, 1);
        }

        template<size_t SIZE, typename T>
        void readBuffer(T* buf, size_t count) {
            static_assert(std::is_integral<T>(), "");
            static_assert(sizeof(T) == SIZE, "");
            directRead(buf, count);
        }

        template<typename T>
        void readBits(T&, size_t) {
            static_assert(std::is_void<T>::value,
                          "Bit-packing is not enabled.\nEnable by call to `enableBitPacking`) or create Deserializer with bit packing enabled.");
        }

        void align() {
        }

        void currentReadPos(size_t pos) {
            this->_adapter.currentReadPos(pos);
        }

        size_t currentReadPos() const {
            return this->_adapter.currentReadPos();
        }

        void currentReadEndPos(size_t pos) {
            this->_adapter.currentReadEndPos(pos);
        }

        size_t currentReadEndPos() const {
            return this->_adapter.currentReadEndPos();
        }

        bool isCompletedSuccessfully() const {
            return this->_adapter.isCompletedSuccessfully();
        }

        ReaderError error() const {
            return this->_adapter.error();
        }

        void error(ReaderError error) {
            this->_adapter.error(error);
        }

        using typename details::AdapterAndContext<InputAdapter, Config, Context>::TValue;
    private:

        template<typename T>
        void directRead(T *v, size_t count) {

            static_assert(!std::is_const<T>::value, "");
            this->_adapter.read(reinterpret_cast<TValue *>(v), sizeof(T) * count);
            //swap each byte if necessary
            _swapDataBits(v, count, std::integral_constant<bool,
                    Config::NetworkEndianness != details::getSystemEndianness()>{});
        }

        template<typename T>
        void _swapDataBits(T *v, size_t count, std::true_type) {
            std::for_each(v, std::next(v, count), [this](T &x) { x = details::swap(x); });
        }

        template<typename T>
        void _swapDataBits(T *, size_t , std::false_type) {
            //empty function because no swap is required
        }

    };

    template<typename TReader>
    class AdapterReaderBitPackingWrapper: public details::AdapterAndContextWrapper<TReader> {
    public:

        using details::AdapterAndContextWrapper<TReader>::AdapterAndContextWrapper;

        static constexpr bool BitPackingEnabled = true;

        ~AdapterReaderBitPackingWrapper() {
            align();
        }

        template<size_t SIZE, typename T>
        void readBytes(T &v) {
            static_assert(std::is_integral<T>(), "");
            static_assert(sizeof(T) == SIZE, "");
            using UT = typename std::make_unsigned<T>::type;
            if (!m_scratchBits)
                this->_wrapped.template readBytes<SIZE,T>(v);
            else
                readBits(reinterpret_cast<UT &>(v), details::BitsSize<T>::value);
        }

        template<size_t SIZE, typename T>
        void readBuffer(T *buf, size_t count) {
            static_assert(std::is_integral<T>(), "");
            static_assert(sizeof(T) == SIZE, "");

            if (!m_scratchBits) {
                this->_wrapped.template readBuffer<SIZE,T>(buf, count);
            } else {
                using UT = typename std::make_unsigned<T>::type;
                //todo improve implementation
                const auto end = buf + count;
                for (auto it = buf; it != end; ++it)
                    readBits(reinterpret_cast<UT &>(*it), details::BitsSize<T>::value);
            }
        }

        template<typename T>
        void readBits(T &v, size_t bitsCount) {
            static_assert(std::is_integral<T>() && std::is_unsigned<T>(), "");
            readBitsInternal(v, bitsCount);
        }

        void align() {
            if (m_scratchBits) {
                ScratchType tmp{};
                readBitsInternal(tmp, m_scratchBits);
                if (tmp)
                    error(ReaderError::InvalidData);
            }
        }

        void currentReadPos(size_t pos) {
            align();
            this->_wrapped.currentReadPos(pos);
        }

        size_t currentReadPos() const {
            return this->_wrapped.currentReadPos();
        }

        void currentReadEndPos(size_t pos) {
            this->_wrapped.currentReadEndPos(pos);
        }

        size_t currentReadEndPos() const {
            return this->_wrapped.currentReadEndPos();
        }

        bool isCompletedSuccessfully() const {
            return this->_wrapped.isCompletedSuccessfully();
        }

        ReaderError error() const {
            return this->_wrapped.error();
        }

        void error(ReaderError error) {
            this->_wrapped.error(error);
        }

    private:
        using UnsignedValue = typename std::make_unsigned<typename TReader::TValue>::type;
        using ScratchType = typename details::ScratchType<UnsignedValue>::type;

        ScratchType m_scratch{};
        size_t m_scratchBits{};

        template<typename T>
        void readBitsInternal(T &v, size_t size) {
            auto bitsLeft = size;
            T res{};
            while (bitsLeft > 0) {
                auto bits = (std::min)(bitsLeft, details::BitsSize<UnsignedValue>::value);
                if (m_scratchBits < bits) {
                    UnsignedValue tmp;
                    this->_wrapped.template readBytes<sizeof(UnsignedValue), UnsignedValue>(tmp);
                    m_scratch |= static_cast<ScratchType>(tmp) << m_scratchBits;
                    m_scratchBits += details::BitsSize<UnsignedValue>::value;
                }
                auto shiftedRes =
                        static_cast<T>(m_scratch & ((static_cast<ScratchType>(1) << bits) - 1)) << (size - bitsLeft);
                res |= shiftedRes;
                m_scratch >>= bits;
                m_scratchBits -= bits;
                bitsLeft -= bits;
            }
            v = res;
        }

    };

    namespace details {
        // used in "making friends" with non-wrapped deserializer type
        template <typename TReader>
        struct GetNonWrappedAdapterReader {
            using Reader = TReader;
        };

        template <typename TWrapped>
        struct GetNonWrappedAdapterReader<AdapterReaderBitPackingWrapper<TWrapped>> {
            using Reader = TWrapped;
        };
    }

}

#endif //BITSERY_ADAPTER_READER_H
