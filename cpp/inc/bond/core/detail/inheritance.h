// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

namespace bond
{


template <typename Input> 
struct base_input
{
    BOOST_STATIC_ASSERT(is_reference<Input>::value);
    typedef Input type;
    static type from(type input)
    {
        return input;
    }
};


namespace detail
{

template <typename T, typename Enable = void> struct 
hierarchy_depth
{
    static const uint16_t value = 1;
};


template <typename T> struct 
hierarchy_depth<T, typename boost::enable_if<is_class<typename schema<typename T::base>::type> >::type>
{
    static const uint16_t value = 1 + hierarchy_depth<typename schema<typename T::base>::type>::value;
};


template <typename T> struct 
expected_depth
{
    static const uint16_t value = 0xffff;
};


template <typename T> struct 
expected_depth<bond::To<T> >
{
    static const uint16_t value = hierarchy_depth<typename schema<T>::type>::value;
};


template <typename Input, typename T = void, typename Enable = void> struct 
is_reader 
    : false_type {};


template <typename Input, typename T> struct 
is_reader<Input&, T, typename boost::enable_if<is_class<typename Input::Parser> >::type>
    : true_type {};


template <typename Base, typename T>
inline Base& base_cast(T& obj)
{
    return static_cast<Base&>(obj);
}


template <typename Base, typename T>
inline const Base& base_cast(const T& obj)
{
    return static_cast<const Base&>(obj);
}


template <typename Input, typename Parser>
class ParserInheritance
    : boost::noncopyable
{
protected:
    ParserInheritance(Input input, bool base)
        : _input(input),
          _base(base)
    {}

    // use compile-time schema
    template <typename T, typename Transform>
    typename boost::enable_if_c<(hierarchy_depth<T>::value > expected_depth<Transform>::value), bool>::type
    Read(const T&, const Transform& transform)
    {
        typename base_input<Input>::type base(base_input<Input>::from(_input));

        // The hierarchy of the payload schema is deeper than what the transform "expects".
        // We recursively find the matching level to start parsing from.
        // After we finish parsing the expected parts of the hierarchy, we give 
        // the parser a chance to skip the unexpected parts.
        detail::StructBegin(_input, true);

        bool result = Parser(base, _base).Read(typename schema<typename T::base>::type(), transform);

        detail::StructEnd(_input, true);

        static_cast<Parser*>(this)->SkipFields(typename boost::mpl::begin<typename T::fields>::type());

        return result;
    }

    
    template <typename T, typename Transform>
    typename boost::disable_if_c<(hierarchy_depth<T>::value > expected_depth<Transform>::value), bool>::type
    Read(const T&, const Transform& transform)
    {
        // We are at the expected level within the hierarchy.
        // First we recurse into base structs (serialized data starts at the top of the hierarchy)
        // and then we read to the transform the fields of the top level struct.
        transform.Begin(T::metadata);
        ReadBase(base_class<T>(), transform);
        bool result = static_cast<Parser*>(this)->ReadFields(typename boost::mpl::begin<typename T::fields>::type(), transform);
        transform.End();
        return result;
    }


    template <typename Base, typename Transform>
    typename boost::enable_if<is_reader<Input, Base>, bool>::type
    ReadBase(const Base*, const Transform& transform)
    {
        typename base_input<Input>::type base(base_input<Input>::from(_input));

        return transform.Base(bonded<Base, Input>(base, true));
    }


    template <typename Base, typename Transform>
    typename boost::disable_if<is_reader<Input, Base>, bool>::type
    ReadBase(const Base*, const Transform& transform)
    {
        return transform.Base(base_cast<Base>(_input));
    }


    template <typename Transform>
    bool ReadBase(const no_base*, const Transform&)
    {
        return false;
    }


    // use runtime schema
    template <typename Transform>
    bool Read(const RuntimeSchema& schema, const Transform& transform)
    {
        // The logic is the same as for compile-time schemas, described in the comments above.
        bool result;

        typename base_input<Input>::type base(base_input<Input>::from(_input));

        if (schema_depth(schema) > expected_depth<Transform>::value)
        {
            BOOST_ASSERT(schema.HasBase());

            detail::StructBegin(_input, true);

            result = Parser(base, _base).Read(schema.GetBaseSchema(), transform);

            detail::StructEnd(_input, true);

            static_cast<Parser*>(this)->SkipFields(schema);
        }
        else
        {
            transform.Begin(schema.GetStruct().metadata);
            
            if (schema.HasBase())
                transform.Base(bonded<void, Input>(base, schema.GetBaseSchema(), true));

            result = static_cast<Parser*>(this)->ReadFields(schema, transform);
            transform.End();
        }

        return result;
    }

    Input _input;
    const bool _base;
};

} // namespace detail

} // namespace bond
