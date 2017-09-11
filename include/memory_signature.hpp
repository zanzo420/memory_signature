/*
* Copyright 2017 Justas Masiulis
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef JM_MEMORY_SIGNATURE_HPP
#define JM_MEMORY_SIGNATURE_HPP

#include <algorithm>   // search
#include <memory>      // unique_ptr
#include <bitset>      // bitset

/// \brief main namespace
namespace jm {

    /// \brief Internal implementation namespace
    namespace detail {

        template<class ForwardIt, class Cmp>
        inline unsigned char find_wildcard(ForwardIt first, ForwardIt last, Cmp cmp)
        {
            std::bitset<256> bits;

            for (; first != last; ++first)
                bits[static_cast<unsigned char>(*first)] = cmp(*first);

            for (auto i = 0; i < 256; ++i)
                if (!bits[i])
                    return static_cast<unsigned char>(i);

            throw std::range_error("unable to find unused byte in the provided pattern");
        }

        template<class ForwardIt1, class ForwardIt2>
        inline auto find_wildcard_masked(ForwardIt1 first, ForwardIt1 last, ForwardIt2 mask_first, unsigned char unk)
        {
            return find_wildcard(first, last, [=](auto) mutable {
                return *mask_first++ != unk;
            });
        }

        template<class ForwardIt>
        inline auto find_wildcard_hybrid(ForwardIt first, ForwardIt last)
        {
            return find_wildcard(first, last, [](auto b) { return !(b != ' ' && b != '?'); });
        }

    }

    /// \brief A light wrapper class around a memory signature providing an easy way to search for it in memory
    class memory_signature {
        std::unique_ptr<unsigned char[]> _pattern;
        unsigned char                    *_end;
        unsigned char                    _wildcard;

        /// \private
        std::size_t size() const noexcept { return _end - _pattern.get(); }

        /// \private
        template<class ForwardIt1, class ForwardIt2>
        void masked_to_wildcard(ForwardIt1 first, ForwardIt1 last, ForwardIt2 m_first, unsigned char unknown) noexcept
        {
            auto my_pat = _pattern.get();
            for (; first != last; ++first, ++m_first, ++my_pat) {
                if (*m_first != unknown)
                    *my_pat = *first;
                else
                    *my_pat = _wildcard;
            }
        }

        /// \private
        template<class ForwardIt>
        void hybrid_to_wildcard(ForwardIt first, ForwardIt last)
        {
            char tokens[2] = {0};
            int  n_tokens  = 0; // do not move - will act as a delimiter for tokens
            auto my_pat    = _pattern.get();

            for (bool prev_was_wildcard = false; first != last; ++first) {
                if (*first == ' ') {
                    prev_was_wildcard = false;
                    if (!n_tokens)
                        continue;

                    n_tokens = 0; // will act as delimiter for the string
                    *my_pat++ = std::strtoul(tokens, nullptr, 16);
                    tokens[1] = 0; // reset it in case it isn't replaced
                }
                else if (*first == '?') {
                    if (!prev_was_wildcard) {
                        *my_pat++ = _wildcard;
                        prev_was_wildcard = true;
                    }
                }
                else
                    tokens[n_tokens++] = *first;
            }

            if (n_tokens)
                *my_pat++ = std::strtoul(tokens, nullptr, 16);

            _end = my_pat;
        }

    public:
        /// \brief Construct a new signature that is empty
        /// \throw Nothrow guarantee
        explicit constexpr memory_signature() noexcept
                : _pattern(nullptr)
                , _end(nullptr)
                , _wildcard(0) {}

        /// \brief Destroys the stored signature
        ~memory_signature() noexcept = default;

        /// \brief copy constructor
        /// \throw Strong exception safety guarantee
        memory_signature(const memory_signature &other)
                : _pattern(std::make_unique<unsigned char[]>(other.size()))
                , _end(_pattern.get() + other.size())
                , _wildcard(other._wildcard)
        {
            std::copy(other._pattern.get(), other._end, _pattern.get());
        }

        /// \brief copy assignment operator
        /// \throw Strong exception safety guarantee
        memory_signature &operator=(const memory_signature &other)
        {
            _pattern = std::make_unique<unsigned char[]>(other.size());
            std::copy(other._pattern.get(), other._end, _pattern.get());
            _end      = _pattern.get() + other.size();
            _wildcard = other._wildcard;
            return *this;
        }

        /// \brief Move constructor
        /// \throw Nothrow guarantee
        memory_signature(memory_signature &&other) noexcept
                : _pattern(std::move(other._pattern))
                , _end(other._end)
                , _wildcard(other._wildcard) {}

        /// \brief Move assignment operator
        /// \throw Nothrow guarantee
        memory_signature &operator=(memory_signature &&other) noexcept
        {
            _pattern  = std::move(other._pattern);
            _end      = other._end;
            _wildcard = other._wildcard;
            return *this;
        }

        /// \brief Construct a new signature using a pattern and a wildcard.
        /// \param pattern The pattern in the form of an integral initializer list.
        /// \param wildcard The value to represent an unknown byte in the pattern.
        /// \code{.cpp}
        /// // will match any byte sequence where first byte is 0x11, third is 0x13 and fourth is 0x14
        /// memory_signature{{0x11, 0x12, 0x13, 0x14"}, 0x12};
        /// \endcode
        template<class Wildcard>
        memory_signature(std::initializer_list<Wildcard> pattern, Wildcard wildcard)
                : _pattern(std::make_unique<unsigned char[]>(pattern.size()))
                , _end(_pattern.get() + pattern.size())
                , _wildcard(static_cast<unsigned char>(wildcard))
        {
            std::copy(std::begin(pattern), std::end(pattern), _pattern.get());
        }

        /// masked signature constructors ------------------------------------------------------------------------------

        /// \brief Construct a new signature using a pattern and a mask.
        /// \param pattern The pattern in the form of an integral initializer list.
        /// \param mask The mask for pattern as a string where value of unknown_byte_identifier
        ///             is the unknown byte which can be anything in the pattern. By default '?'.
        /// \param unknown_byte_identifier The value to represent an unknown byte in the mask.
        /// \code{.cpp}
        /// // will match any byte sequence where first byte is 0x11, third is 0x13 and fourth is 0x14
        /// memory_signature{{0x11, 0x12, 0x13, 0x14}, "x?xx", '?'};
        /// \endcode
        template<class Byte>
        memory_signature(std::initializer_list<Byte> pattern, const std::string &mask
                         , Byte unknown_byte_identifier = '?')
                : _pattern(std::make_unique<unsigned char[]>(pattern.size()))
                , _end(_pattern.get() + pattern.size())
                , _wildcard(detail::find_wildcard_masked(pattern.begin(), pattern.end(), mask.begin()
                                                         , unknown_byte_identifier))
        {
            if (pattern.size() != mask.size())
                throw std::invalid_argument("pattern size did not match mask size");

            masked_to_wildcard(pattern.begin(), pattern.end(), mask.begin(), unknown_byte_identifier);
        }

        /// \brief Construct a new signature using a pattern and a mask.
        /// \param pattern The pattern in the form of an integral initializer list.
        /// \param mask The mask for pattern as an integral initializer list where value of unknown_byte_identifier
        ///             is the unknown byte which can be anything in the pattern. By default 0.
        /// \param unknown_byte_identifier The value to represent an unknown byte in the mask.
        /// \code{.cpp}
        /// // will match any byte sequence where first byte is 0x11, third is 0x13 and fourth is 0x14
        /// memory_signature{{0x11, 0x12, 0x13, 0x14}, {1, 0, 1, 1}, 0};
        /// \endcode
        template<class Byte>
        memory_signature(std::initializer_list<Byte> pattern, std::initializer_list<Byte> mask
                         , Byte unknown_byte_identifier = 0)
                : _pattern(std::make_unique<unsigned char[]>(pattern.size()))
                , _end(_pattern.get() + pattern.size())
                , _wildcard(detail::find_wildcard_masked(pattern.begin(), pattern.end(), mask.begin()
                                                         , unknown_byte_identifier))
        {
            if (pattern.size() != mask.size())
                throw std::invalid_argument("pattern size did not match mask size");

            masked_to_wildcard(pattern.begin(), pattern.end(), mask.begin(), unknown_byte_identifier);
        }

        /// hybrid / ida style signature constructors ------------------------------------------------------------------

        /// \brief Construct a new ida style signature.
        /// \param pattern The pattern in the form of a string where numbers and letter are transformed to known bytes
        ///                 and question marks are unknown bytes.
        /// \code{.cpp}
        /// // will match any byte sequence where first byte is 0x1, third is 0x13 and fourth is 0x14
        /// memory_signature{"01 ?? 13 14"};
        /// memory_signature{"1 ? 13 14"};
        /// \endcode
        memory_signature(const std::string &pattern)
                : _pattern(std::make_unique<unsigned char[]>(pattern.size()))
                , _end(_pattern.get() + pattern.size())
                , _wildcard(detail::find_wildcard_hybrid(pattern.begin(), pattern.end()))
        {
            hybrid_to_wildcard(pattern.begin(), pattern.end());
        }

        /// \brief Searches for first occurrence of stored signature in the range [first, last - signature_length).
        /// \param first The first element of the range in which to search for.
        /// \param last The one past the last element of the range in which to search for.
        /// \return Returns iterator to the beginning of signature.
        ///         If no such signature is found or if signature is empty returns last.
        template<class ForwardIt>
        ForwardIt find(ForwardIt first, ForwardIt last) const
        {
            if (_pattern.get() == _end)
                return last;

            return std::search(first, last, _pattern.get(), _end, [wildcard = _wildcard](unsigned char lhs
                                                                                         , unsigned char rhs) {
                return lhs == rhs || rhs == wildcard;
            });
        }
    };

}

#endif // include guard
