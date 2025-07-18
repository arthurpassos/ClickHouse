#pragma once

#include <Columns/ColumnVector.h>
#include <Columns/ColumnDecimal.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnFixedString.h>
#include <Columns/ColumnConst.h>
#include <Columns/ColumnNullable.h>

#include <Common/typeid_cast.h>
#include <Common/UTF8Helpers.h>

#include <DataTypes/EnumValues.h>

#include <Functions/GatherUtils/IArraySource.h>
#include <Functions/GatherUtils/IValueSource.h>
#include <Functions/GatherUtils/Slices.h>
#include <Functions/FunctionHelpers.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int NOT_IMPLEMENTED;
    extern const int ILLEGAL_COLUMN;
}

namespace GatherUtils
{
template <typename T> struct NumericArraySink;
struct StringSink;
struct FixedStringSink;
struct GenericArraySink;
template <typename ArraySink> struct NullableArraySink;

template <typename T>
struct NumericArraySource : public ArraySourceImpl<NumericArraySource<T>>
{
    using ColVecType = ColumnVectorOrDecimal<T>;
    using Slice = NumericArraySlice<T>;
    using Column = ColumnArray;

    using SinkType = NumericArraySink<T>;

    const ColVecType & column;
    const typename ColVecType::Container & elements;
    const typename ColumnArray::Offsets & offsets;

    size_t row_num = 0;
    ColumnArray::Offset prev_offset = 0;

    MutableColumnPtr createValuesColumn()
    {
        return column.cloneEmpty();
    }

    explicit NumericArraySource(const ColumnArray & arr)
        : column(typeid_cast<const ColVecType &>(arr.getData()))
        , elements(typeid_cast<const ColVecType &>(arr.getData()).getData()), offsets(arr.getOffsets())
    {
    }

    void next()
    {
        prev_offset = offsets[row_num];
        ++row_num;
    }

    bool isEnd() const
    {
        return row_num == offsets.size();
    }

    size_t rowNum() const
    {
        return row_num;
    }

    const typename ColumnArray::Offsets & getOffsets() const override
    {
        return offsets;
    }

    /// Get size for corresponding call or Sink::reserve to reserve memory for elements.
    size_t getSizeForReserve() const override
    {
        return elements.size();
    }

    size_t getColumnSize() const override
    {
        return offsets.size();
    }

    size_t getElementSize() const
    {
        return offsets[row_num] - prev_offset;
    }

    Slice getWhole() const
    {
        return {&elements[prev_offset], offsets[row_num] - prev_offset};
    }

    Slice getSliceFromLeft(size_t offset) const
    {
        size_t elem_size = offsets[row_num] - prev_offset;
        if (offset >= elem_size)
            return {&elements[prev_offset], 0};
        return {&elements[prev_offset + offset], elem_size - offset};
    }

    Slice getSliceFromLeft(size_t offset, size_t length) const
    {
        size_t elem_size = offsets[row_num] - prev_offset;
        if (offset >= elem_size)
            return {&elements[prev_offset], 0};
        return {&elements[prev_offset + offset], std::min(length, elem_size - offset)};
    }

    Slice getSliceFromRight(size_t offset) const
    {
        size_t elem_size = offsets[row_num] - prev_offset;
        if (offset > elem_size)
            return {&elements[prev_offset], elem_size};
        return {&elements[offsets[row_num] - offset], offset};
    }

    Slice getSliceFromRight(size_t offset, size_t length) const
    {
        size_t elem_size = offsets[row_num] - prev_offset;
        if (offset > elem_size)
            return {&elements[prev_offset], length + elem_size > offset ? std::min(elem_size, length + elem_size - offset) : 0};
        return {&elements[offsets[row_num] - offset], std::min(length, offset)};
    }
};


/// The methods can be virtual or not depending on the template parameter. See IStringSource.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsuggest-override"
#pragma clang diagnostic ignored "-Wsuggest-destructor-override"

/// NOLINTBEGIN(hicpp-use-override, modernize-use-override)

template <typename Base>
struct ConstSource : public Base
{
    using Slice = typename Base::Slice;
    using Column = ColumnConst;

    using SinkType = typename Base::SinkType;

    size_t total_rows;
    size_t row_num = 0;

    explicit ConstSource(const ColumnConst & col_)
        : Base(static_cast<const typename Base::Column &>(col_.getDataColumn()))
        , total_rows(col_.size())
    {
    }

    template <typename ColumnType>
    ConstSource(const ColumnType & col_, size_t total_rows_)
        : Base(col_)
        , total_rows(total_rows_)
    {
    }

    template <typename ColumnType>
    ConstSource(const ColumnType & col_, const NullMap & null_map_, size_t total_rows_)
        : Base(col_, null_map_)
        , total_rows(total_rows_)
    {
    }

    ConstSource(const ConstSource &) = default;
    virtual ~ConstSource() = default;

    virtual void accept(ArraySourceVisitor & visitor) // override
    {
        if constexpr (std::is_base_of_v<IArraySource, Base>)
            visitor.visit(*this);
        else
            throw Exception(ErrorCodes::NOT_IMPLEMENTED,
                            "accept(ArraySourceVisitor &) is not implemented for {} because {} "
                            "is not derived from IArraySource",
                            demangle(typeid(ConstSource<Base>).name()), demangle(typeid(Base).name()));
    }

    virtual void accept(ValueSourceVisitor & visitor) // override
    {
        if constexpr (std::is_base_of_v<IValueSource, Base>)
            visitor.visit(*this);
        else
            throw Exception(ErrorCodes::NOT_IMPLEMENTED,
                            "accept(ValueSourceVisitor &) is not implemented for {} because {} "
                            "is not derived from IValueSource",
                            demangle(typeid(ConstSource<Base>).name()), demangle(typeid(Base).name()));
    }

    void next()
    {
        ++row_num;
    }

    bool isEnd() const
    {
        return row_num == total_rows;
    }

    size_t rowNum() const
    {
        return row_num;
    }

    size_t getSizeForReserve() const
    {
        return total_rows * Base::getSizeForReserve();
    }

    size_t getColumnSize() const
    {
        return total_rows;
    }

    bool isConst() const
    {
        return true;
    }
};

/// NOLINTEND(hicpp-use-override, modernize-use-override)

#pragma clang diagnostic pop

struct StringSource
{
    using Slice = NumericArraySlice<UInt8>;
    using Column = ColumnString;

    using SinkType = StringSink;

    const typename ColumnString::Chars & elements;
    const typename ColumnString::Offsets & offsets;

    size_t row_num = 0;
    ColumnString::Offset prev_offset = 0;

    explicit StringSource(const ColumnString & col)
        : elements(col.getChars())
        , offsets(col.getOffsets())
    {
    }

    void next()
    {
        prev_offset = offsets[row_num];
        ++row_num;
    }

    bool isEnd() const
    {
        return row_num == offsets.size();
    }

    size_t rowNum() const
    {
        return row_num;
    }

    size_t getSizeForReserve() const
    {
        return elements.size();
    }

    size_t getElementSize() const
    {
        return offsets[row_num] - prev_offset - 1;
    }

    size_t getColumnSize() const
    {
        return offsets.size();
    }

    Slice getWhole() const
    {
        return {&elements[prev_offset], offsets[row_num] - prev_offset - 1};
    }

    Slice getSliceFromLeft(size_t offset) const
    {
        size_t elem_size = offsets[row_num] - prev_offset - 1;
        if (offset >= elem_size)
            return {&elements[prev_offset], 0};
        return {&elements[prev_offset + offset], elem_size - offset};
    }

    Slice getSliceFromLeft(size_t offset, size_t length) const
    {
        size_t elem_size = offsets[row_num] - prev_offset - 1;
        if (offset >= elem_size)
            return {&elements[prev_offset], 0};
        return {&elements[prev_offset + offset], std::min(length, elem_size - offset)};
    }

    Slice getSliceFromRight(size_t offset) const
    {
        size_t elem_size = offsets[row_num] - prev_offset - 1;
        if (offset > elem_size)
            return {&elements[prev_offset], elem_size};
        return {&elements[prev_offset + elem_size - offset], offset};
    }

    Slice getSliceFromRight(size_t offset, size_t length) const
    {
        size_t elem_size = offsets[row_num] - prev_offset - 1;
        if (offset > elem_size)
            return {&elements[prev_offset], length + elem_size > offset ? std::min(elem_size, length + elem_size - offset) : 0};
        return {&elements[prev_offset + elem_size - offset], std::min(length, offset)};
    }

    const ColumnString::Chars & getElements() const { return elements; }
};

/// Treats Enum values as Strings, modeled after StringSource
template <typename EnumDataType>
struct EnumSource
{
    using Column = typename EnumDataType::ColumnType;
    using Slice = NumericArraySlice<UInt8>;

    using SinkType = StringSink;

    const typename Column::Container & data;
    const EnumDataType & data_type;

    size_t row_num = 0;

    EnumSource(const Column & col, const EnumDataType & data_type_)
        : data(col.getData())
        , data_type(data_type_)
    {
    }

    void next()
    {
        ++row_num;
    }

    bool isEnd() const
    {
        return row_num == data.size();
    }

    size_t rowNum() const
    {
        return row_num;
    }

    size_t getSizeForReserve() const
    {
        return data.size();
    }

    size_t getElementSize() const
    {
        std::string_view name = data_type.getNameForValue(data[row_num]).toView();
        return name.size();
    }

    size_t getColumnSize() const
    {
        return data.size();
    }

    Slice getWhole() const
    {
        std::string_view name = data_type.getNameForValue(data[row_num]).toView();
        return {reinterpret_cast<const UInt8 *>(name.data()), name.size()};
    }

    Slice getSliceFromLeft(size_t offset) const
    {
        std::string_view name = data_type.getNameForValue(data[row_num]).toView();
        if (offset >= name.size())
            return {reinterpret_cast<const UInt8 *>(name.data()), 0};
        return {reinterpret_cast<const UInt8 *>(name.data()) + offset, name.size() - offset};
    }

    Slice getSliceFromLeft(size_t offset, size_t length) const
    {
        std::string_view name = data_type.getNameForValue(data[row_num]).toView();
        if (offset >= name.size())
            return {reinterpret_cast<const UInt8 *>(name.data()), 0};
        return {reinterpret_cast<const UInt8 *>(name.data()) + offset, std::min(length, name.size() - offset)};
    }

    Slice getSliceFromRight(size_t offset) const
    {
        std::string_view name = data_type.getNameForValue(data[row_num]).toView();
        if (offset > name.size())
            return {reinterpret_cast<const UInt8 *>(name.data()), name.size()};
        return {reinterpret_cast<const UInt8 *>(name.data()) + name.size() - offset, offset};
    }

    Slice getSliceFromRight(size_t offset, size_t length) const
    {
        std::string_view name = data_type.getNameForValue(data[row_num]).toView();
        if (offset > name.size())
            return {reinterpret_cast<const UInt8 *>(name.data()), length + name.size() > offset ? std::min(name.size(), length + name.size() - offset) : 0};
        return {reinterpret_cast<const UInt8 *>(name.data()) + name.size() - offset, std::min(length, offset)};
    }
};


/// Differs to StringSource by having 'offset' and 'length' in code points instead of bytes in getSlice* methods.
/** NOTE: The behaviour of substring and substringUTF8 is inconsistent when negative offset is greater than string size:
  * substring:
  *      hello
  * ^-----^ - offset -10, length 7, result: "he"
  * substringUTF8:
  *      hello
  *      ^-----^ - offset -10, length 7, result: "hello"
  * This may be subject for change.
  */
struct UTF8StringSource : public StringSource
{
    using StringSource::StringSource;

    static const ColumnString::Char * skipCodePointsForward(const ColumnString::Char * pos, size_t size, const ColumnString::Char * end)
    {
        for (size_t i = 0; i < size && pos < end; ++i)
            pos += UTF8::seqLength(*pos);   /// NOTE pos may become greater than end. It is Ok due to padding in PaddedPODArray.
        return pos;
    }

    static const ColumnString::Char * skipCodePointsBackward(const ColumnString::Char * pos, size_t size, const ColumnString::Char * begin)
    {
        for (size_t i = 0; i < size && pos > begin; ++i)
        {
            --pos;
            if (pos == begin)
                break;
            UTF8::syncBackward(pos, begin);
        }
        return pos;
    }

    size_t getElementSize() const
    {
        return UTF8::countCodePoints(&elements[prev_offset], StringSource::getElementSize());
    }

    Slice getSliceFromLeft(size_t offset) const
    {
        const auto * begin = &elements[prev_offset];
        const auto * end = elements.data() + offsets[row_num] - 1;
        const auto * res_begin = skipCodePointsForward(begin, offset, end);

        if (res_begin >= end)
            return {begin, 0};

        return {res_begin, size_t(end - res_begin)};
    }

    Slice getSliceFromLeft(size_t offset, size_t length) const
    {
        const auto * begin = &elements[prev_offset];
        const auto * end = elements.data() + offsets[row_num] - 1;
        const auto * res_begin = skipCodePointsForward(begin, offset, end);

        if (res_begin >= end)
            return {begin, 0};

        const auto * res_end = skipCodePointsForward(res_begin, length, end);

        if (res_end >= end)
            return {res_begin, size_t(end - res_begin)};

        return {res_begin, size_t(res_end - res_begin)};
    }

    Slice getSliceFromRight(size_t offset) const
    {
        const auto * begin = &elements[prev_offset];
        const auto * end = elements.data() + offsets[row_num] - 1;
        const auto * res_begin = skipCodePointsBackward(end, offset, begin);

        return {res_begin, size_t(end - res_begin)};
    }

    Slice getSliceFromRight(size_t offset, size_t length) const
    {
        const auto * begin = &elements[prev_offset];
        const auto * end = elements.data() + offsets[row_num] - 1;
        const auto * res_begin = skipCodePointsBackward(end, offset, begin);
        const auto * res_end = skipCodePointsForward(res_begin, length, end);

        if (res_end >= end)
            return {res_begin, size_t(end - res_begin)};

        return {res_begin, size_t(res_end - res_begin)};
    }
};


struct FixedStringSource
{
    using Slice = NumericArraySlice<UInt8>;
    using Column = ColumnFixedString;

    using SinkType = FixedStringSink;

    const UInt8 * pos;
    const UInt8 * end;
    size_t string_size;
    const typename ColumnString::Chars & elements;

    size_t row_num = 0;
    size_t column_size = 0;

    explicit FixedStringSource(const ColumnFixedString & col) : string_size(col.getN()), elements(col.getChars())
    {
        const auto & chars = col.getChars();
        pos = chars.data();
        end = pos + chars.size();
        column_size = col.size();
    }

    void next()
    {
        pos += string_size;
        ++row_num;
    }

    bool isEnd() const
    {
        return pos == end;
    }

    size_t rowNum() const
    {
        return row_num;
    }

    size_t getSizeForReserve() const
    {
        return end - pos;
    }

    size_t getElementSize() const
    {
        return string_size;
    }

    size_t getColumnSize() const
    {
        return column_size;
    }

    Slice getWhole() const
    {
        return {pos, string_size};
    }

    Slice getSliceFromLeft(size_t offset) const
    {
        if (offset >= string_size)
            return {pos, 0};
        return {pos + offset, string_size - offset};
    }

    Slice getSliceFromLeft(size_t offset, size_t length) const
    {
        if (offset >= string_size)
            return {pos, 0};
        return {pos + offset, std::min(length, string_size - offset)};
    }

    Slice getSliceFromRight(size_t offset) const
    {
        if (offset > string_size)
            return {pos, string_size};
        return {pos + string_size - offset, offset};
    }

    Slice getSliceFromRight(size_t offset, size_t length) const
    {
        if (offset > string_size)
            return {pos, length + string_size > offset ? std::min(string_size, length + string_size - offset) : 0};
        return {pos + string_size - offset, std::min(length, offset)};
    }

    const ColumnString::Chars & getElements() const { return elements; }
};


struct IStringSource
{
    using Slice = NumericArraySlice<UInt8>;

    virtual void next() = 0;
    virtual bool isEnd() const = 0;
    virtual size_t getSizeForReserve() const = 0;
    virtual Slice getWhole() const = 0;
    virtual ~IStringSource() = default;
};


template <typename Impl>
struct DynamicStringSource final : IStringSource
{
    Impl impl;

    explicit DynamicStringSource(const IColumn & col) : impl(static_cast<const typename Impl::Column &>(col)) {}

    void next() override { impl.next(); }
    bool isEnd() const override { return impl.isEnd(); }
    size_t getSizeForReserve() const override { return impl.getSizeForReserve(); }
    Slice getWhole() const override { return impl.getWhole(); }
};

inline std::unique_ptr<IStringSource> createDynamicStringSource(const IColumn & col)
{
    if (checkColumn<ColumnString>(&col))
        return std::make_unique<DynamicStringSource<StringSource>>(col);
    if (checkColumn<ColumnFixedString>(&col))
        return std::make_unique<DynamicStringSource<FixedStringSource>>(col);
    if (checkColumnConst<ColumnString>(&col))
        return std::make_unique<DynamicStringSource<ConstSource<StringSource>>>(col);
    if (checkColumnConst<ColumnFixedString>(&col))
        return std::make_unique<DynamicStringSource<ConstSource<FixedStringSource>>>(col);
    throw Exception(ErrorCodes::ILLEGAL_COLUMN, "Unexpected type of string column: {}", col.getName());
}

using StringSources = std::vector<std::unique_ptr<IStringSource>>;


struct GenericArraySource : public ArraySourceImpl<GenericArraySource>
{
    using Slice = GenericArraySlice;
    using Column = ColumnArray;

    using SinkType = GenericArraySink;

    const IColumn & elements;
    const typename ColumnArray::Offsets & offsets;

    size_t row_num = 0;
    ColumnArray::Offset prev_offset = 0;

    MutableColumnPtr createValuesColumn()
    {
        return elements.cloneEmpty();
    }

    explicit GenericArraySource(const ColumnArray & arr)
        : elements(arr.getData())
        , offsets(arr.getOffsets())
    {
    }

    void next()
    {
        prev_offset = offsets[row_num];
        ++row_num;
    }

    bool isEnd() const
    {
        return row_num == offsets.size();
    }

    size_t rowNum() const
    {
        return row_num;
    }

    const typename ColumnArray::Offsets & getOffsets() const override
    {
        return offsets;
    }

    size_t getSizeForReserve() const override
    {
        return elements.size();
    }

    size_t getColumnSize() const override
    {
        return offsets.size();
    }

    size_t getElementSize() const
    {
        return offsets[row_num] - prev_offset;
    }

    Slice getWhole() const
    {
        return {&elements, prev_offset, offsets[row_num] - prev_offset};
    }

    Slice getSliceFromLeft(size_t offset) const
    {
        size_t elem_size = offsets[row_num] - prev_offset;
        if (offset >= elem_size)
            return {&elements, prev_offset, 0};
        return {&elements, prev_offset + offset, elem_size - offset};
    }

    Slice getSliceFromLeft(size_t offset, size_t length) const
    {
        size_t elem_size = offsets[row_num] - prev_offset;
        if (offset >= elem_size)
            return {&elements, prev_offset, 0};
        return {&elements, prev_offset + offset, std::min(length, elem_size - offset)};
    }

    Slice getSliceFromRight(size_t offset) const
    {
        size_t elem_size = offsets[row_num] - prev_offset;
        if (offset > elem_size)
            return {&elements, prev_offset, elem_size};
        return {&elements, offsets[row_num] - offset, offset};
    }

    Slice getSliceFromRight(size_t offset, size_t length) const
    {
        size_t elem_size = offsets[row_num] - prev_offset;
        if (offset > elem_size)
            return {&elements, prev_offset, length + elem_size > offset ? std::min(elem_size, length + elem_size - offset) : 0};
        return {&elements, offsets[row_num] - offset, std::min(length, offset)};
    }
};


template <typename ArraySource>
struct NullableArraySource : public ArraySource
{
    using Slice = NullableSlice<typename ArraySource::Slice>;
    using ArraySource::prev_offset;
    using ArraySource::row_num;
    using ArraySource::offsets;

    using SinkType = NullableArraySink<typename ArraySource::SinkType>;

    const NullMap & null_map;

    NullableArraySource(const ColumnArray & arr, const NullMap & null_map_)
            : ArraySource(arr), null_map(null_map_)
    {
    }

    MutableColumnPtr createValuesColumn()
    {
        return ColumnNullable::create(static_cast<ArraySource *>(this)->createValuesColumn(), ColumnUInt8::create());
    }

    void accept(ArraySourceVisitor & visitor) override { visitor.visit(*this); }

    Slice getWhole() const
    {
        Slice slice = ArraySource::getWhole();
        slice.null_map = &null_map[prev_offset];
        return slice;
    }

    Slice getSliceFromLeft(size_t offset) const
    {
        Slice slice = ArraySource::getSliceFromLeft(offset);
        if (offsets[row_num] > prev_offset + offset)
            slice.null_map = &null_map[prev_offset + offset];
        else
            slice.null_map = &null_map[prev_offset];
        return slice;
    }

    Slice getSliceFromLeft(size_t offset, size_t length) const
    {
        Slice slice = ArraySource::getSliceFromLeft(offset, length);
        if (offsets[row_num] > prev_offset + offset)
            slice.null_map = &null_map[prev_offset + offset];
        else
            slice.null_map = &null_map[prev_offset];
        return slice;
    }

    Slice getSliceFromRight(size_t offset) const
    {
        Slice slice = ArraySource::getSliceFromRight(offset);
        if (offsets[row_num] > prev_offset + offset)
            slice.null_map = &null_map[offsets[row_num] - offset];
        else
            slice.null_map = &null_map[prev_offset];
        return slice;
    }

    Slice getSliceFromRight(size_t offset, size_t length) const
    {
        Slice slice = ArraySource::getSliceFromRight(offset, length);
        if (offsets[row_num] > prev_offset + offset)
            slice.null_map = &null_map[offsets[row_num] - offset];
        else
            slice.null_map = &null_map[prev_offset];
        return slice;
    }

    bool isNullable() const override
    {
        return true;
    }
};


template <typename T>
struct NumericValueSource : ValueSourceImpl<NumericValueSource<T>>
{
    using Slice = NumericValueSlice<T>;
    using Column = ColumnVectorOrDecimal<T>;

    using SinkType = NumericArraySink<T>;

    const T * begin;
    size_t total_rows;
    size_t row_num = 0;

    explicit NumericValueSource(const Column & col)
    {
        const auto & container = col.getData();
        begin = container.data();
        total_rows = container.size();
    }

    void next()
    {
        ++row_num;
    }

    bool isEnd() const
    {
        return row_num == total_rows;
    }

    size_t rowNum() const
    {
        return row_num;
    }

    size_t getSizeForReserve() const
    {
        return total_rows;
    }

    Slice getWhole() const
    {
        Slice slice;
        slice.value = begin[row_num];
        return slice;
    }
};

struct GenericValueSource : public ValueSourceImpl<GenericValueSource>
{
    using Slice = GenericValueSlice;
    using SinkType = GenericArraySink;
    using Column = IColumn;

    const IColumn * column;
    size_t total_rows;
    size_t row_num = 0;

    explicit GenericValueSource(const IColumn & col)
    {
        column = &col;
        total_rows = col.size();
    }

    void next()
    {
        ++row_num;
    }

    bool isEnd() const
    {
        return row_num == total_rows;
    }

    size_t rowNum() const
    {
        return row_num;
    }

    size_t getSizeForReserve() const
    {
        return total_rows;
    }

    Slice getWhole() const
    {
        Slice slice;
        slice.elements = column;
        slice.position = row_num;
        return slice;
    }
};

template <typename ValueSource>
struct NullableValueSource : public ValueSource
{
    using SinkType = NullableArraySink<typename ValueSource::SinkType>;

    using Slice = NullableSlice<typename ValueSource::Slice>;
    using ValueSource::row_num;

    const NullMap & null_map;

    template <typename Column>
    NullableValueSource(const Column & col, const NullMap & null_map_)
        : ValueSource(col)
        , null_map(null_map_)
    {}

    void accept(ValueSourceVisitor & visitor) override { visitor.visit(*this); }

    Slice getWhole() const
    {
        Slice slice = ValueSource::getWhole();
        slice.null_map = null_map.data() + row_num;
        return slice;
    }
};

}
}
