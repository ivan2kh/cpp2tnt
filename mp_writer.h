#ifndef MP_WRITER_H
#define MP_WRITER_H

#include <string_view>
#include <array>
#include "msgpuck/msgpuck.h"
#include "wtf_buffer.h"

namespace tnt {
class connection;
enum class request_type : uint8_t;
}

/** Helper to fill requests buffer.
 *
 * A caller must ensure there is enough free space in the buffer.
 */
class mp_writer
{
public:
    /// Wrap writer object around cn.output_buffer().
    mp_writer(tnt::connection &cn);
    /// Wrap writer object around specified buffer.
    mp_writer(tnt::connection &cn, wtf_buffer &buf);
    /// Initiate request of specified type. A caller must compose a body afterwards and call end()
    /// to finalize request.
    void encode_header(tnt::request_type req_type);
    /// Put authentication request into the underlying buffer. User and password
    /// from connection string are used.
    void encode_auth_request();
    /// Put authentication request into the underlying buffer.
    void encode_auth_request(std::string_view user, std::string_view password);
    /// Put ping request into the underlying buffer.
    void encode_ping_request();

    /// Initiate call request. A caller must fill arguments afterwards and call end()
    /// to finalize request.
    void begin_call(std::string_view fn_name);
    /// Put array header with <max_size> cardinality and move current position over it.
    /// If max_cardinality > 0 then call end() to replace initial cardinality with actual value.
    void begin_array(uint32_t max_cardinality);
    /// Put map header with <max_size> cardinality and move current position over it.
    /// If max_cardinality > 0 then call end() to replace initial cardinality with actual value.
    void begin_map(uint32_t max_cardinality);
    /// Replace initial cardinality (or request size) settled with begin_array(), begin_map(),
    /// begin_call() or encode_header() with actual value (counted untill now). Do not finalize
    /// containers with initial zero size.
    void end();

    mp_writer& operator<< (const std::string_view &val);

    template <typename T>
    mp_writer& operator<< (const std::optional<T> &val) noexcept
    {
        if (!val.has_value())
        {
            _buf.end = mp_encode_nil(_buf.end);

            if (!_opened_containers.empty())
                ++_opened_containers.top().items_count;
            return *this;
        }
        operator<<(val.value());
        return *this;
    }

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && sizeof(T) < 16>>
    mp_writer& operator<< (const T &val) noexcept
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            _buf.end = mp_encode_bool(_buf.end, val);
        }
        else
        {
            _buf.end = (val >= 0 ?
                        mp_encode_uint(_buf.end, static_cast<uint64_t>(val)) :
                        mp_encode_int(_buf.end, val));
        }

        if (!_opened_containers.empty())
            ++_opened_containers.top().items_count;
        return *this;
    }

private:
    template <typename T, std::size_t N = 16>
    class small_stack
    {
    private:
        std::array<T, N> _items;
        size_t _size = 0;
    public:
        small_stack() = default;
        void push(T &&value)
        {
            _items[_size++] = std::move(value);
        }
        T& pop() noexcept  // undefined if empty
        {
            if (!_size)
                return _items[_size];
            return _items[--_size];
        }
        T& top() noexcept  // undefined if empty
        {
            return  _items[_size ? _size - 1 : _size];
        }
        size_t size() const noexcept
        {
            return _size;
        }
        bool empty() const noexcept
        {
            return _size == 0;
        }
    };

    struct container_meta
    {
        size_t head_offset; // we'll detect container type by the first byte
        uint32_t max_cardinality;
        uint32_t items_count = 0; // map will have 2x items
    };

    small_stack<container_meta> _opened_containers;
    tnt::connection &_cn; ///< request id, initial user name and password source
    wtf_buffer &_buf;
};

#endif // MP_WRITER_H
