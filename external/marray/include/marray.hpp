#ifndef _MARRAY_MARRAY_HPP_
#define _MARRAY_MARRAY_HPP_

#include <type_traits>
#include <array>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <utility>

#include "miterator.hpp"
#include "utility.hpp"

#define VECTOR_ALIGNMENT 16

#ifndef MARRAY_BASE_ALIGNMENT
#define MARRAY_BASE_ALIGNMENT 64
#endif

#ifndef MARRAY_STRIDE_ALIGNMENT
#define MARRAY_STRIDE_ALIGNMENT VECTOR_ALIGNMENT
#endif

#ifndef MARRAY_BLAS_PROTOTYPED
extern "C"
{

void sgemm_(const char* transa, const char* transb,
            const int* m, const int* n, const int* k,
            const float* alpha, const float* A, const int* lda,
                                const float* B, const int* ldb,
            const float*  beta,       float* C, const int* ldc);

void dgemm_(const char* transa, const char* transb,
            const int* m, const int* n, const int* k,
            const double* alpha, const double* A, const int* lda,
                                 const double* B, const int* ldb,
            const double*  beta,       double* C, const int* ldc);

}
#endif

namespace MArray
{
    namespace detail
    {
        inline size_t align(size_t n, size_t alignment)
        {
            return ((n+alignment-1)/alignment)*alignment;
        }
    }

    template <typename T, size_t N> struct aligned_allocator
    {
        typedef T value_type;

        aligned_allocator() {}
        template <typename U, size_t M> aligned_allocator(const aligned_allocator<U, M>& other) {}

        T* allocate(size_t n)
        {
            void* ptr;
            int ret = posix_memalign(&ptr, N, n*sizeof(T));
            if (ret != 0) throw std::bad_alloc();
            return (T*)ptr;
        }

        void deallocate(T* ptr, size_t n)
        {
            free(ptr);
        }

        template<class U>
        struct rebind { typedef aligned_allocator<U, N> other; };
    };

    template <typename T, size_t N, typename U, size_t M>
    bool operator==(const aligned_allocator<T, N>&, const aligned_allocator<U, M>&) { return true; }

    template <typename T, size_t N, typename U, size_t M>
    bool operator!=(const aligned_allocator<T, N>&, const aligned_allocator<U, M>&) { return false; }

    namespace slice
    {
        /*
         * The type all_t specifies a range [0,len_i) for an array
         * dimension i of length len_i (i.e. it selects all of the data along
         * that dimension).
         */
        struct all_t { constexpr all_t() {} };
        constexpr all_t all;
    }

    /*
     * The special value uninitialized is used to construct an array which
     * does not default- or value-initialize its elements (useful for avoiding
     * redundant memory operations for scalar types).
     */
    struct uninitialized_t { constexpr uninitialized_t() {} };
    constexpr uninitialized_t uninitialized;

    struct transpose_t { constexpr transpose_t() {} };
    namespace transpose { constexpr transpose_t T; }

    /*
     * Specifies the layout of the array data.
     */
    class Layout
    {
        public:
            enum {COLUMN_MAJOR, ROW_MAJOR, DEFAULT=ROW_MAJOR};

            bool operator==(Layout other)
            {
                return type == other.type;
            }

            bool operator!=(Layout other)
            {
                return type != other.type;
            }

        protected:
            int type;

            constexpr Layout(int type) : type(type) {}
    };

    class RowMajorLayout : public Layout
    {
        public:
            constexpr RowMajorLayout() : Layout(Layout::ROW_MAJOR) {}
    };

    constexpr RowMajorLayout ROW_MAJOR;

    class ColumnMajorLayout : public Layout
    {
        public:
            constexpr ColumnMajorLayout() : Layout(Layout::COLUMN_MAJOR) {}
    };

    constexpr ColumnMajorLayout COLUMN_MAJOR;

    class DefaultLayout : public Layout
    {
        public:
            constexpr DefaultLayout() : Layout(Layout::DEFAULT) {}
    };

    constexpr DefaultLayout DEFAULT;

    namespace detail
    {
        template <bool Cond, typename T=void>
        using enable_if_t = typename std::enable_if<Cond,T>::type;

        template <bool Cond, typename T, typename U>
        using conditional_t = typename std::conditional<Cond,T,U>::type;

        template <typename... Args> struct types {};

        template <typename Types1, typename Types2>
        struct concat_types;

        template <typename... Args1, typename... Args2>
        struct concat_types<types<Args1...>, types<Args2...>>
        {
            typedef types<Args1..., Args2...> type;
        };

        template <typename Types1, typename Types2>
        using concat_types_t = typename concat_types<Types1, Types2>::type;

        template <unsigned First, unsigned Pos, typename... Args>
        struct trailing_types_helper
        {
            typedef types<> type;
        };

        template <unsigned First, unsigned Pos, typename Arg, typename... Args>
        struct trailing_types_helper<First, Pos, Arg, Args...>
        {
            typedef typename trailing_types_helper<First, Pos+1, Args...>::type type;
        };

        template <unsigned First, typename Arg, typename... Args>
        struct trailing_types_helper<First, First, Arg, Args...>
        {
            typedef types<Arg, Args...> type;
        };

        template <unsigned First, unsigned Pos, typename... Args>
        using trailing_types_helper_t = typename trailing_types_helper<First, Pos, Args...>::type;

        template <unsigned First, typename... Args>
        struct trailing_types
        {
            typedef conditional_t<(First >= sizeof...(Args)),
                                  types<>,
                                  trailing_types_helper_t<First, 0, Args...>> type;
        };

        template <unsigned First, typename... Args>
        struct trailing_types<First, types<Args...>> : trailing_types<First, Args...> {};

        template <unsigned First, typename... Args>
        using trailing_types_t = typename trailing_types<First, Args...>::type;

        template <unsigned Last, unsigned Pos, typename... Args>
        struct leading_types_helper
        {
            typedef types<> type;
        };

        template <unsigned Last, unsigned Pos, typename Arg, typename... Args>
        struct leading_types_helper<Last, Pos, Arg, Args...>
        {
            typedef concat_types_t<types<Arg>, typename leading_types_helper<Last, Pos+1, Args...>::type> type;
        };

        template <unsigned Last, typename Arg, typename... Args>
        struct leading_types_helper<Last, Last, Arg, Args...>
        {
            typedef types<> type;
        };

        template <unsigned Last, unsigned Pos, typename... Args>
        using leading_types_helper_t = typename leading_types_helper<Last, Pos, Args...>::type;

        template <unsigned Last, typename... Args>
        struct leading_types
        {
            typedef conditional_t<(Last >= sizeof...(Args)),
                                  types<Args...>,
                                  leading_types_helper_t<Last, 0, Args...>> type;
        };

        template <unsigned Last, typename... Args>
        struct leading_types<Last, types<Args...>> : leading_types<Last, Args...> {};

        template <unsigned Last, typename... Args>
        using leading_types_t = typename leading_types<Last, Args...>::type;

        template <unsigned First, unsigned Last, typename... Args>
        struct middle_types
        {
            typedef conditional_t<(sizeof...(Args)-First > Last),
                                  trailing_types_t<First, leading_types_t<Last, Args...>>,
                                  leading_types_t<Last-First, trailing_types_t<First, Args...>>> type;
        };

        template <unsigned First, unsigned Last, typename... Args>
        using middle_types_t = typename middle_types<First, Last, Args...>::type;

        template <unsigned N, typename... Args>
        using nth_type_t = middle_types_t<N, N+1, Args...>;

        struct apply_some_args
        {
            template <typename Types1>
            struct leading;
        };

        template <typename... Args>
        struct apply_some_args::leading<types<Args...>>
        {
            template <typename Types>
            struct middle;
        };

        template <typename... Args1>
        template <typename... Args2>
        struct apply_some_args::leading<types<Args1...>>::middle<types<Args2...>>
        {
            template <typename Types>
            struct trailing;
        };

        template <typename... Args1>
        template <typename... Args2>
        template <typename... Args3>
        struct apply_some_args::leading<types<Args1...>>::middle<types<Args2...>>::trailing<types<Args3...>>
        {
            template <typename Func, size_t Size=sizeof...(Args2)>
            enable_if_t<Size == 0, typename std::result_of<Func(Args2&&...)>::type>
            operator()(Func&& func, Args1&&... args1, Args2&&... args2, Args3&&... args3) const
            {
                return func();
            }

            template <typename Func, size_t Size=sizeof...(Args2)>
            enable_if_t<Size != 0, typename std::result_of<Func(Args2&&...)>::type>
            operator()(Func&& func, Args1&&... args1, Args2&&... args2, Args3&&... args3) const
            {
                return func(std::forward<Args2>(args2)...);
            }
        };

        template <unsigned First>
        struct apply_trailing_args
        {
            template <typename Func, typename... Args>
            auto operator()(Func&& func, Args&&... args) ->
            decltype(typename
                     apply_some_args::template leading<leading_types_t<First, Args...>>
                                    ::template middle<trailing_types_t<First, Args...>>
                                    ::template trailing<types<>>
                         ()(std::forward<Func>(func), std::forward<Args>(args)...)) const
            {
                return typename
                apply_some_args::template leading<leading_types_t<First, Args...>>
                               ::template middle<trailing_types_t<First, Args...>>
                               ::template trailing<types<>>
                    ()(std::forward<Func>(func), std::forward<Args>(args)...);
            }
        };

        template <unsigned Last>
        struct apply_leading_args
        {
            template <typename Func, typename... Args>
            auto operator()(Func&& func, Args&&... args) ->
            decltype(typename
                     apply_some_args::template leading<types<>>
                                    ::template middle<leading_types_t<Last, Args...>>
                                    ::template trailing<trailing_types_t<Last, Args...>>
                         ()(std::forward<Func>(func), std::forward<Args>(args)...)) const
            {
                return typename
                apply_some_args::template leading<types<>>
                               ::template middle<leading_types_t<Last, Args...>>
                               ::template trailing<trailing_types_t<Last, Args...>>
                    ()(std::forward<Func>(func), std::forward<Args>(args)...);
            }
        };

        template <unsigned First, unsigned Last>
        struct apply_middle_args
        {
            template <typename Func, typename... Args>
            auto operator()(Func&& func, Args&&... args) ->
            decltype(typename
                     apply_some_args::template leading<leading_types_t<First, Args...>>
                                    ::template middle<middle_types_t<First, Last, Args...>>
                                    ::template trailing<trailing_types_t<Last, Args...>>
                         ()(std::forward<Func>(func), std::forward<Args>(args)...)) const
            {
                return typename
                apply_some_args::template leading<leading_types_t<First, Args...>>
                               ::template middle<middle_types_t<First, Last, Args...>>
                               ::template trailing<trailing_types_t<Last, Args...>>
                    ()(std::forward<Func>(func), std::forward<Args>(args)...);
            }
        };

        template <unsigned N>
        using apply_nth_arg = apply_middle_args<N, N+1>;

        template <typename T, typename Types>
        struct make_array_from_types;

        template <typename T, typename... Args>
        struct make_array_from_types<T, types<Args...>>
        {
            static std::array<T, sizeof...(Args)> apply(Args&&... args)
            {
                return {{(T)std::forward<Args>(args)...}};
            }
        };

        template <typename T, typename Types>
        struct make_vector_from_types;

        template <typename T, typename... Args>
        struct make_vector_from_types<T, types<Args...>>
        {
            static std::vector<T> apply(Args&&... args)
            {
                return {{(T)std::forward<Args>(args)...}};
            }
        };

        /*
        template <typename... Parameters>
        struct check_types
        {
            template <template <typename...> class Condition, typename... Args>
            struct do_check;
        };

        template <typename... Parameters>
        template <template <typename...> class Condition>
        struct check_types<Parameters...>::do_check<Condition>
        : std::true_type {};

        template <typename... Parameters>
        template <template <typename...> class Condition, typename Arg>
        struct check_types<Parameters...>::do_check<Condition, Arg>
        : std::integral_constant<bool, Condition<Arg, Parameters...>::value> {};

        template <typename... Parameters>
        template <template <typename...> class Condition, typename Arg, typename... Args>
        struct check_types<Parameters...>::do_check<Condition, Arg, Args...>
        : std::integral_constant<bool, Condition<Arg, Parameters...>::value &&
                                       check_types<Parameters...>::template do_check<Condition, Args...>::value> {};

        template <typename... Parameters>
        template <template <typename...> class Condition, typename... Args>
        struct check_types<Parameters...>::do_check<Condition, types<Args...>>
        : check_types<Parameters...>::template do_check<Condition, Args...> {};
        */

        template <typename T, typename... Args>
        struct are_convertible;

        template <typename T>
        struct are_convertible<T> : std::true_type {};

        template <typename T, typename Arg, typename... Args>
        struct are_convertible<T, Arg, Args...> :
            conditional_t<std::is_convertible<Arg, T>::value,
                          are_convertible<T, Args...>,
                          std::false_type> {};

        template <typename T, typename... Args>
        struct are_convertible<T, types<Args...>> : are_convertible<T, Args...> {};

        //template <typename T, typename... Args>
        //struct are_convertible : check_types<T>::template do_check<std::is_convertible, Args...> {};

        template <typename T, typename=void>
        struct is_index_or_slice_helper : std::false_type {};

        template <typename T>
        struct is_index_or_slice_helper<T, enable_if_t<std::is_convertible<T, int>::value>> : std::true_type {};

        template <typename I>
        struct is_index_or_slice_helper<range_t<I>> : std::true_type {};

        template <>
        struct is_index_or_slice_helper<slice::all_t> : std::true_type {};

        template <typename T>
        struct is_index_or_slice : is_index_or_slice_helper<typename std::decay<T>::type> {};

        template <typename... Args>
        struct are_indices_or_slices;

        template<>
        struct are_indices_or_slices<> : std::true_type {};

        template <typename Arg, typename... Args>
        struct are_indices_or_slices<Arg, Args...> :
            conditional_t<is_index_or_slice<Arg>::value,
                          are_indices_or_slices<Args...>,
                          std::false_type> {};

        template <typename... Args>
        struct are_indices_or_slices<types<Args...>> : are_indices_or_slices<Args...> {};

        //template <typename... Args>
        //struct are_indices_or_slices : check_types<>::template do_check<is_index_or_slice, Args...> {};
    }

    template <typename T, unsigned ndim>
    class const_marray_view;

    template <typename T, unsigned ndim>
    class marray_view;

    template <typename T, unsigned ndim, typename Allocator>
    class marray;

    template <typename T, unsigned ndim> void copy(const const_marray_view<T, ndim>& a, marray_view<T, ndim>& b);
    template <typename T, unsigned ndim> void copy(const const_marray_view<T, ndim>& a, marray_view<T, ndim>&& b);

    namespace detail
    {
        template <typename T, unsigned ndim, unsigned dim>
        class const_marray_ref;

        template <typename T, unsigned ndim, unsigned dim>
        class marray_ref;

        template <typename T, unsigned ndim, unsigned dim, unsigned newdim>
        class const_marray_slice;

        template <typename T, unsigned ndim, unsigned dim, unsigned newdim>
        class marray_slice;

        /*
         * Represents a part of an array, where the first dim-1 out of ndim
         * dimensions have been indexed into. This type may be implicity converted
         * to an array view or further indexed. This particular reference type
         * is explicitly const, and may only be used to read the original array.
         */
        template <typename T, unsigned ndim, unsigned dim>
        class const_marray_ref
        {
            template <typename T_, unsigned ndim_> friend class MArray::const_marray_view;
            template <typename T_, unsigned ndim_> friend class MArray::marray_view;
            template <typename T_, unsigned ndim_, typename Allocator_> friend class MArray::marray;
            template <typename T_, unsigned ndim_, unsigned dim_> friend class const_marray_ref;
            template <typename T_, unsigned ndim_, unsigned dim_> friend class marray_ref;
            template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class const_marray_slice;
            template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class marray_slice;

            protected:
                typedef marray_view<T, ndim> base;

                typedef typename base::stride_type stride_type;
                typedef typename base::idx_type idx_type;
                typedef typename base::value_type value_type;
                typedef typename base::pointer pointer;
                typedef typename base::const_pointer const_pointer;
                typedef typename base::reference reference;
                typedef typename base::const_reference const_reference;

                base& array;
                stride_type idx;

                const_marray_ref(const const_marray_ref& other) = default;

                const_marray_ref(const marray_view<T, ndim>& array, stride_type idx, idx_type i)
                : array(const_cast<base&>(array)), idx(idx+i*array.stride_[dim-2]) {}

                const_marray_ref& operator=(const const_marray_ref&) = delete;

            public:
                template <int diff=ndim-dim>
                typename std::enable_if<diff==0, const T&>::type
                operator[](idx_type i) const
                {
                    return data()[i];
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff!=0, const_marray_ref<T, ndim, dim+1>>::type
                operator[](idx_type i) const
                {
                    assert(i >= 0 && i < array.len_[dim-1]);
                    return {array, idx, i};
                }

                template <typename I, int diff=ndim-dim>
                typename std::enable_if<diff==0, const_marray_view<T, 1>>::type
                operator[](const range_t<I>& x) const
                {
                    assert(x.front() <= x.back() && x.front() >= 0 && x.back() <= array.len_[ndim-1]);
                    return {x.size(), data()+array.stride_[ndim-1]*x.front(), array.stride_[ndim-1]};
                }

                template <typename I, int diff=ndim-dim>
                typename std::enable_if<diff!=0, const_marray_slice<T, ndim, dim+1, 1>>::type
                operator[](const range_t<I>& x) const
                {
                    return {array, idx, {}, {}, x};
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff==0, const_marray_view<T, 1>>::type
                operator[](slice::all_t x) const
                {
                    return *this;
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff!=0, const_marray_slice<T, ndim, dim+1, 1>>::type
                operator[](slice::all_t x) const
                {
                    return {array, idx, {}, {}, range(idx_type(), array.len_[dim-1])};
                }

                template <typename Arg, typename=
                    typename std::enable_if<is_index_or_slice<Arg>::value>::type>
                auto operator()(Arg&& arg) const ->
                decltype((*this)[std::forward<Arg>(arg)])
                {
                    return (*this)[std::forward<Arg>(arg)];
                }

                template <typename Arg, typename... Args, typename=
                    typename std::enable_if<ndim-dim != 0 &&
                                            sizeof...(Args) == ndim-dim &&
                                            are_indices_or_slices<Arg, Args...>::value>::type>
                auto operator()(Arg&& arg, Args&&... args) const ->
                decltype((*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...))
                {
                    return (*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...);
                }

                const_pointer data() const
                {
                    return array.data_+idx;
                }

                operator const_marray_view<T, ndim-dim+1>() const
                {
                    std::array<idx_type, ndim-dim+1> len;
                    std::array<stride_type, ndim-dim+1> stride;
                    std::copy_n(array.len_.begin()+dim-1, ndim-dim+1, len.begin());
                    std::copy_n(array.stride_.begin()+dim-1, ndim-dim+1, stride.begin());
                    return {len, data(), stride};
                }
        };

        template <typename T, unsigned ndim, unsigned dim>
        class marray_ref : public const_marray_ref<T, ndim, dim>
        {
            template <typename T_, unsigned ndim_> friend class MArray::const_marray_view;
            template <typename T_, unsigned ndim_> friend class MArray::marray_view;
            template <typename T_, unsigned ndim_, typename Allocator_> friend class MArray::marray;
            template <typename T_, unsigned ndim_, unsigned dim_> friend class const_marray_ref;
            template <typename T_, unsigned ndim_, unsigned dim_> friend class marray_ref;
            template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class const_marray_slice;
            template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class marray_slice;

            protected:
                typedef marray_view<T, ndim> base;
                typedef const_marray_ref<T, ndim, dim> parent;

                typedef typename base::stride_type stride_type;
                typedef typename base::idx_type idx_type;
                typedef typename base::value_type value_type;
                typedef typename base::pointer pointer;
                typedef typename base::const_pointer const_pointer;
                typedef typename base::reference reference;
                typedef typename base::const_reference const_reference;

                using parent::array;
                using parent::idx;

                marray_ref(const parent& other)
                : parent(other) {}

                marray_ref(const marray_ref& other) = default;

                marray_ref(marray_view<T, ndim>& array, stride_type idx, idx_type i)
                : parent(array, idx, i) {}

                marray_ref(marray_view<T, ndim>&& array, stride_type idx, idx_type i)
                : parent(array, idx, i) {}

            public:
                marray_ref& operator=(const parent& other)
                {
                    copy(view(other), view(*this));
                    return *this;
                }

                marray_ref& operator=(const marray_ref& other)
                {
                    copy(view(other), view(*this));
                    return *this;
                }

                template <unsigned ndim_>
                marray_ref& operator=(const const_marray_ref<T, ndim_, ndim_-ndim+dim>& other)
                {
                    copy(view(other), view(*this));
                    return *this;
                }

                template <unsigned ndim_, unsigned newdim_>
                marray_ref& operator=(const const_marray_slice<T, ndim_, ndim_-ndim+dim+newdim_, newdim_>& other)
                {
                    copy(view(other), view(*this));
                    return *this;
                }

                using parent::operator[];

                template <int diff=ndim-dim>
                typename std::enable_if<diff==0, T&>::type
                operator[](idx_type i)
                {
                    return const_cast<T&>(parent::operator[](i));
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff!=0, marray_ref<T, ndim, dim+1>>::type
                operator[](idx_type i)
                {
                    return parent::operator[](i);
                }

                template <typename I, int diff=ndim-dim>
                typename std::enable_if<diff==0, marray_view<T, 1>>::type
                operator[](const range_t<I>& x)
                {
                    return parent::operator[](x);
                }

                template <typename I, int diff=ndim-dim>
                typename std::enable_if<diff!=0, marray_slice<T, ndim, dim+1, 1>>::type
                operator[](const range_t<I>& x)
                {
                    return parent::operator[](x);
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff==0, marray_view<T, 1>>::type
                operator[](slice::all_t x)
                {
                    return parent::operator[](x);
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff!=0, marray_slice<T, ndim, dim+1, 1>>::type
                operator[](slice::all_t x)
                {
                    return parent::operator[](x);
                }

                using parent::operator();

                template <typename Arg, typename=
                    typename std::enable_if<is_index_or_slice<Arg>::value>::type>
                auto operator()(Arg&& arg) ->
                decltype((*this)[std::forward<Arg>(arg)])
                {
                    return (*this)[std::forward<Arg>(arg)];
                }

                template <typename Arg, typename... Args, typename=
                    typename std::enable_if<ndim-dim != 0 &&
                                            sizeof...(Args) == ndim-dim &&
                                            are_indices_or_slices<Arg, Args...>::value>::type>
                auto operator()(Arg&& arg, Args&&... args) ->
                decltype((*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...))
                {
                    return (*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...);
                }

                using parent::data;

                pointer data()
                {
                    return const_cast<pointer>(parent::data());
                }

                using parent::operator const_marray_view<T, ndim-dim+1>;

                operator marray_view<T, ndim-dim+1>()
                {
                    return parent::operator const_marray_view<T, ndim-dim+1>();
                }
        };

        template <typename T, unsigned ndim, unsigned dim>
        const_marray_view<T, ndim-dim+1> view(const const_marray_ref<T, ndim, dim>& x)
        {
            return x;
        }

        template <typename T, unsigned ndim, unsigned dim>
        marray_view<T, ndim-dim+1> view(marray_ref<T, ndim, dim>& x)
        {
            return x;
        }

        template <typename T, unsigned ndim, unsigned dim>
        marray_view<T, ndim-dim+1> view(marray_ref<T, ndim, dim>&& x)
        {
            return x;
        }

        /*
         * Represents a part of an array, where the first dim-1 out of ndim
         * dimensions have either been indexed into (i.e. a single value
         * specified for that index) or sliced (i.e. a range of values specified).
         * The parameter newdim specifies how many indices were sliced. The
         * reference may be converted into an array view (of dimension
         * ndim-dim+1+newdim) or further indexed, but may not be used to modify
         * data.
         */
        template <typename T, unsigned ndim, unsigned dim, unsigned newdim>
        class const_marray_slice
        {
            template <typename T_, unsigned ndim_> friend class MArray::const_marray_view;
            template <typename T_, unsigned ndim_> friend class MArray::marray_view;
            template <typename T_, unsigned ndim_, typename Allocator_> friend class MArray::marray;
            template <typename T_, unsigned ndim_, unsigned dim_> friend class const_marray_ref;
            template <typename T_, unsigned ndim_, unsigned dim_> friend class marray_ref;
            template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class const_marray_slice;
            template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class marray_slice;

            protected:
                typedef marray_view<T, ndim> base;

                typedef typename base::stride_type stride_type;
                typedef typename base::idx_type idx_type;
                typedef typename base::value_type value_type;
                typedef typename base::pointer pointer;
                typedef typename base::const_pointer const_pointer;
                typedef typename base::reference reference;
                typedef typename base::const_reference const_reference;

                base& array;
                stride_type idx;
                std::array<unsigned, newdim> dims;
                std::array<idx_type, newdim> lens;

                const_marray_slice(const const_marray_slice& other) = default;

                const_marray_slice(const marray_view<T, ndim>& array, stride_type idx,
                                   const std::array<unsigned,newdim>& dims,
                                   const std::array<idx_type,newdim>& lens, idx_type i)
                : array(const_cast<base&>(array)), idx(idx+i*array.stride_[dim-2]), dims(dims), lens(lens) {}

                template <typename I>
                const_marray_slice(const marray_view<T, ndim>& array, stride_type idx,
                                   const std::array<unsigned,newdim-1>& dims,
                                   const std::array<idx_type,newdim-1>& lens, const range_t<I>& range_)
                : array(const_cast<base&>(array)), idx(idx+array.stride_[dim-2]*range_.front())
                {
                    std::copy(dims.begin(), dims.end(), this->dims.begin());
                    this->dims.back() = dim-2;
                    std::copy(lens.begin(), lens.end(), this->lens.begin());
                    this->lens.back() = range_.size();
                }

                const_marray_slice& operator=(const const_marray_slice&) = delete;

            public:
                template <int diff=ndim-dim>
                typename std::enable_if<diff==0, const_marray_view<T, newdim>>::type
                operator[](idx_type i) const
                {
                    assert(i >= 0 && i < array.len_[ndim-1]);
                    std::array<stride_type, newdim> strides;
                    for (unsigned i = 0;i < newdim;i++)
                    {
                        strides[i] = array.stride_[dims[i]];
                    }
                    return {lens, data()+i*array.stride_[ndim-1], strides};
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff!=0, const_marray_slice<T, ndim, dim+1, newdim>>::type
                operator[](idx_type i) const
                {
                    assert(i >= 0 && i < array.len_[dim-1]);
                    return {array, idx, dims, lens, i};
                }

                template <typename I, int diff=ndim-dim>
                typename std::enable_if<diff==0, const_marray_view<T, newdim+1>>::type
                operator[](const range_t<I>& x) const
                {
                    assert(x.front() <= x.back() && x.front() >= 0 && x.back() <= array.len_[ndim-1]);
                    std::array<idx_type, newdim+1> newlens;
                    std::array<stride_type, newdim+1> strides;
                    for (unsigned i = 0;i < newdim;i++)
                    {
                        newlens[i] = lens[i];
                        strides[i] = array.stride_[dims[i]];
                    }
                    newlens[newdim] = x.size();
                    strides[newdim] = array.stride_[ndim-1];
                    return {newlens, data()+array.stride_[ndim-1]*x.front(), strides};
                }

                template <typename I, int diff=ndim-dim>
                typename std::enable_if<diff!=0, const_marray_slice<T, ndim, dim+1, newdim+1>>::type
                operator[](const range_t<I>& x) const
                {
                    assert(x.front() <= x.back() && x.front() >= 0 && x.back() <= array.len_[dim-1]);
                    return {array, idx, dims, lens, x};
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff==0, const_marray_view<T, newdim+1>>::type
                operator[](slice::all_t x) const
                {
                    return *this;
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff!=0, const_marray_slice<T, ndim, dim+1, newdim+1>>::type
                operator[](slice::all_t x) const
                {
                    return {array, idx, dims, lens, range(idx_type(), array.len_[dim-1])};
                }

                template <typename Arg, typename=
                    typename std::enable_if<is_index_or_slice<Arg>::value>::type>
                auto operator()(Arg&& arg) const ->
                decltype((*this)[std::forward<Arg>(arg)])
                {
                    return (*this)[std::forward<Arg>(arg)];
                }

                template <typename Arg, typename... Args, typename=
                    typename std::enable_if<ndim-dim != 0 &&
                                            sizeof...(Args) == ndim-dim &&
                                            are_indices_or_slices<Arg, Args...>::value>::type>
                auto operator()(Arg&& arg, Args&&... args) const ->
                decltype((*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...))
                {
                    return (*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...);
                }

                const_pointer data() const
                {
                    return array.data_+idx;
                }

                operator const_marray_view<T, ndim+newdim-dim+1>() const
                {
                    std::array<idx_type, ndim+newdim-dim+1> len;
                    std::array<stride_type, ndim+newdim-dim+1> stride;
                    for (unsigned i = 0;i < newdim;i++)
                    {
                        len[i] = lens[i];
                        stride[i] = array.stride_[dims[i]];
                    }
                    std::copy_n(array.len_.begin()+dim-1, ndim-dim+1, len.begin()+newdim);
                    std::copy_n(array.stride_.begin()+dim-1, ndim-dim+1, stride.begin()+newdim);
                    return {len, data(), stride};
                }
        };

        template <typename T, unsigned ndim, unsigned dim, unsigned newdim>
        class marray_slice : public const_marray_slice<T, ndim, dim, newdim>
        {
            template <typename T_, unsigned ndim_> friend class MArray::const_marray_view;
            template <typename T_, unsigned ndim_> friend class MArray::marray_view;
            template <typename T_, unsigned ndim_, typename Allocator_> friend class MArray::marray;
            template <typename T_, unsigned ndim_, unsigned dim_> friend class const_marray_ref;
            template <typename T_, unsigned ndim_, unsigned dim_> friend class marray_ref;
            template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class const_marray_slice;
            template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class marray_slice;

            protected:
                typedef marray_view<T, ndim> base;
                typedef const_marray_slice<T, ndim, dim, newdim> parent;

                typedef typename base::stride_type stride_type;
                typedef typename base::idx_type idx_type;
                typedef typename base::value_type value_type;
                typedef typename base::pointer pointer;
                typedef typename base::const_pointer const_pointer;
                typedef typename base::reference reference;
                typedef typename base::const_reference const_reference;

                using parent::array;
                using parent::idx;
                using parent::dims;
                using parent::lens;

                marray_slice(const parent& other)
                : parent(other) {}

                marray_slice(const marray_slice& other) = default;

                marray_slice(marray_view<T, ndim>& array, stride_type idx,
                             const std::array<unsigned,newdim>& dims,
                             const std::array<idx_type,newdim>& lens, idx_type i)
                : parent(array, idx, dims, lens, i) {}

                marray_slice(marray_view<T, ndim>&& array, stride_type idx,
                             const std::array<unsigned,newdim>& dims,
                             const std::array<idx_type,newdim>& lens, idx_type i)
                : parent(array, idx, dims, lens, i) {}

                template <typename I>
                marray_slice(marray_view<T, ndim>& array, stride_type idx,
                             const std::array<unsigned,newdim-1>& dims,
                             const std::array<idx_type,newdim-1>& lens, const range_t<I>& range_)
                : parent(array, idx, dims, lens, range_) {}

                template <typename I>
                marray_slice(marray_view<T, ndim>&& array, stride_type idx,
                             const std::array<unsigned,newdim-1>& dims,
                             const std::array<idx_type,newdim-1>& lens, const range_t<I>& range_)
                : parent(array, idx, dims, lens, range_) {}

            public:
                marray_slice& operator=(const parent& other)
                {
                    copy(view(other), view(*this));
                    return *this;
                }

                marray_slice& operator=(const marray_slice& other)
                {
                    copy(view(other), view(*this));
                    return *this;
                }

                template <unsigned ndim_>
                marray_slice& operator=(const const_marray_ref<T, ndim_, ndim_-ndim+dim-newdim>& other)
                {
                    copy(view(other), view(*this));
                    return *this;
                }

                template <unsigned ndim_, unsigned newdim_>
                marray_slice& operator=(const const_marray_slice<T, ndim_, ndim_-ndim+dim-newdim+newdim_, newdim_>& other)
                {
                    copy(view(other), view(*this));
                    return *this;
                }

                using parent::operator[];

                template <int diff=ndim-dim>
                typename std::enable_if<diff==0, marray_view<T, newdim>>::type
                operator[](idx_type i)
                {
                    return parent::operator[](i);
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff!=0, marray_slice<T, ndim, dim+1, newdim>>::type
                operator[](idx_type i)
                {
                    return parent::operator[](i);
                }

                template <typename I, int diff=ndim-dim>
                typename std::enable_if<diff==0, marray_view<T, newdim+1>>::type
                operator[](const range_t<I>& x)
                {
                    return {parent::operator[](x)};
                }

                template <typename I, int diff=ndim-dim>
                typename std::enable_if<diff!=0, marray_slice<T, ndim, dim+1, newdim+1>>::type
                operator[](const range_t<I>& x)
                {
                    return parent::operator[](x);
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff==0, marray_view<T, newdim+1>>::type
                operator[](slice::all_t x)
                {
                    return {parent::operator[](x)};
                }

                template <int diff=ndim-dim>
                typename std::enable_if<diff!=0, marray_slice<T, ndim, dim+1, newdim+1>>::type
                operator[](slice::all_t x)
                {
                    return parent::operator[](x);
                }

                using parent::operator();

                template <typename Arg, typename=
                    typename std::enable_if<is_index_or_slice<Arg>::value>::type>
                auto operator()(Arg&& arg) ->
                decltype((*this)[std::forward<Arg>(arg)])
                {
                    return (*this)[std::forward<Arg>(arg)];
                }

                template <typename Arg, typename... Args, typename=
                    typename std::enable_if<ndim-dim != 0 &&
                                            sizeof...(Args) == ndim-dim &&
                                            are_indices_or_slices<Arg, Args...>::value>::type>
                auto operator()(Arg&& arg, Args&&... args) ->
                decltype((*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...))
                {
                    return (*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...);
                }

                using parent::data;

                pointer data()
                {
                    return const_cast<pointer>(parent::data());
                }

                using parent::operator const_marray_view<T, ndim+newdim-dim+1>;

                operator marray_view<T, ndim+newdim-dim+1>()
                {
                    return {parent::operator const_marray_view<T, ndim+newdim-dim+1>()};
                }
        };

        template <typename T, unsigned ndim, unsigned dim, unsigned newdim>
        const_marray_view<T, ndim+newdim-dim+1> view(const const_marray_slice<T, ndim, dim, newdim>& x)
        {
            return x;
        }

        template <typename T, unsigned ndim, unsigned dim, unsigned newdim>
        marray_view<T, ndim+newdim-dim+1> view(marray_slice<T, ndim, dim, newdim>& x)
        {
            return x;
        }

        template <typename T, unsigned ndim, unsigned dim, unsigned newdim>
        marray_view<T, ndim+newdim-dim+1> view(marray_slice<T, ndim, dim, newdim>&& x)
        {
            return x;
        }
    }

    template <typename T, unsigned ndim>
    class const_marray_view
    {
        static_assert(ndim > 0, "0-dimensional marrays are not allowed.");

        template <typename T_, unsigned ndim_> friend class const_marray_view;
        template <typename T_, unsigned ndim_> friend class marray_view;
        template <typename T_, unsigned ndim_, typename Allocator_> friend class marray;
        template <typename T_, unsigned ndim_, unsigned dim_> friend class detail::const_marray_ref;
        template <typename T_, unsigned ndim_, unsigned dim_> friend class detail::marray_ref;
        template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class detail::const_marray_slice;
        template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class detail::marray_slice;

        public:
            typedef unsigned idx_type;
            typedef size_t size_type;
            typedef ptrdiff_t stride_type;
            typedef T value_type;
            typedef T* pointer;
            typedef const T* const_pointer;
            typedef T& reference;
            typedef const T& const_reference;

        protected:
            pointer data_ = nullptr;
            std::array<idx_type,ndim> len_ = {};
            std::array<stride_type,ndim> stride_ = {};

            const_marray_view& operator=(const const_marray_view& other) = delete;

            template <typename... Args>
            static std::array<idx_type,ndim> get_len_from_args(Args&&... args)
            {
                return detail::apply_leading_args<ndim>()(
                    detail::make_array_from_types<idx_type,
                        detail::leading_types_t<ndim, Args...>>::apply,
                    std::forward<Args>(args)...);
            }

            template <typename... Args>
            static const_pointer get_ptr_from_args(Args&&... args)
            {
                return detail::apply_nth_arg<ndim>()(
                    [](const_pointer x) -> const_pointer { return x; },
                    std::forward<Args>(args)...);
            }

            template <typename... Args>
            static const_reference get_value_from_args(Args&&... args)
            {
                return detail::apply_nth_arg<ndim>()(
                    [](const_reference x) -> const_reference { return x; },
                    std::forward<Args>(args)...);
            }

            template <typename... Args>
            static Layout get_layout_from_args(Args&&... args)
            {
                return detail::apply_nth_arg<ndim+1>()(
                    [](Layout x) -> Layout { return x; },
                    std::forward<Args>(args)...);
            }

            template <typename... Args>
            static Layout get_layout_early_from_args(Args&&... args)
            {
                return detail::apply_nth_arg<ndim>()(
                    [](Layout x) -> Layout { return x; },
                    std::forward<Args>(args)...);
            }

            template <typename... Args>
            static std::array<stride_type,ndim> get_stride_from_args(Args&&... args)
            {
                return detail::apply_trailing_args<sizeof...(Args)-ndim>()(
                    detail::make_array_from_types<stride_type,
                        detail::trailing_types_t<sizeof...(Args)-ndim, Args...>>::apply,
                    std::forward<Args>(args)...);
            }

            template <typename... Args>
            struct starts_with_len : detail::are_convertible<idx_type,
                detail::leading_types_t<ndim, Args...>> {};

            template <typename... Args>
            struct has_const_ptr : detail::are_convertible<const_pointer,
                detail::nth_type_t<ndim, Args...>> {};

            template <typename... Args>
            struct has_ptr : detail::are_convertible<pointer,
                detail::nth_type_t<ndim, Args...>> {};

            template <typename... Args>
            struct has_value : detail::are_convertible<const_reference,
                detail::nth_type_t<ndim, Args...>> {};

            template <typename... Args>
            struct has_uninit : detail::are_convertible<uninitialized_t,
                detail::nth_type_t<ndim, Args...>> {};

            template <typename... Args>
            struct has_layout : detail::are_convertible<Layout,
                detail::nth_type_t<ndim+1, Args...>> {};

            template <typename... Args>
            struct has_layout_early : detail::are_convertible<Layout,
                detail::nth_type_t<ndim, Args...>> {};

            template <typename... Args>
            struct ends_with_stride : detail::are_convertible<stride_type,
                detail::trailing_types_t<(sizeof...(Args) > ndim ? sizeof...(Args)-ndim : 0), Args...>> {};

            /*
            template <typename... Args>
            using starts_with_len = detail::are_convertible<idx_type,
                detail::leading_types_t<ndim, Args...>>;

            template <typename... Args>
            using has_const_ptr = detail::are_convertible<const_pointer,
                detail::nth_type_t<ndim, Args...>>;

            template <typename... Args>
            using has_ptr = detail::are_convertible<pointer,
                detail::nth_type_t<ndim, Args...>>;

            template <typename... Args>
            using has_value = detail::are_convertible<const_reference,
                detail::nth_type_t<ndim, Args...>>;

            template <typename... Args>
            using has_uninit = detail::are_convertible<uninitialized_t,
                detail::nth_type_t<ndim, Args...>>;

            template <typename... Args>
            using has_layout = detail::are_convertible<Layout,
                detail::nth_type_t<ndim+1, Args...>>;

            template <typename... Args>
            using has_layout_early = detail::are_convertible<Layout,
                detail::nth_type_t<ndim, Args...>>;

            template <typename... Args>
            using ends_with_stride = detail::are_convertible<stride_type,
                detail::trailing_types_t<(sizeof...(Args) > ndim ? sizeof...(Args)-ndim : 0), Args...>>;
            */

            void reset()
            {
                data_ = nullptr;
                len_.fill(0);
                stride_.fill(0);
            }

            void reset(const marray_view<T, ndim>& other)
            {
                data_ = other.data_;
                len_ = other.len_;
                stride_ = other.stride_;
            }

            template <typename U>
            void reset(const std::array<U, ndim>& len, const_pointer ptr, Layout layout = DEFAULT)
            {
                reset(len, ptr, default_strides(len, layout));
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim+1 &&
                                starts_with_len<Args...>::value &&
                                has_const_ptr<Args...>::value>
            reset(Args&&... args)
            {
                reset(get_len_from_args(std::forward<Args>(args)...),
                      get_ptr_from_args(std::forward<Args>(args)...));
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim+2 &&
                                starts_with_len<Args...>::value &&
                                has_const_ptr<Args...>::value &&
                                has_layout<Args...>::value>
            reset(Args&&... args)
            {
                reset(get_len_from_args(std::forward<Args>(args)...),
                      get_ptr_from_args(std::forward<Args>(args)...),
                      get_layout_from_args(std::forward<Args>(args)...));
            }

            template <typename U, typename V>
            void reset(const std::array<U, ndim>& len, const_pointer ptr, const std::array<V, ndim>& stride)
            {
                data_ = const_cast<pointer>(ptr);
                std::copy_n(len.begin(), ndim, len_.begin());
                std::copy_n(stride.begin(), ndim, stride_.begin());
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim+1+ndim &&
                                starts_with_len<Args...>::value &&
                                has_const_ptr<Args...>::value &&
                                ends_with_stride<Args...>::value>
            reset(Args&&... args)
            {
                reset(get_len_from_args(std::forward<Args>(args)...),
                      get_ptr_from_args(std::forward<Args>(args)...),
                      get_stride_from_args(std::forward<Args>(args)...));
            }

        public:
            template <typename U>
            static std::array<stride_type, ndim> default_strides(const std::array<U, ndim>& len, Layout layout=DEFAULT)
            {
                std::array<stride_type, ndim> stride;

                if (layout == ROW_MAJOR)
                {
                    stride[ndim-1] = 1;
                    for (unsigned i = ndim-1;i > 0;i--)
                    {
                        stride[i-1] = stride[i]*len[i];
                    }
                }
                else
                {
                    stride[0] = 1;
                    for (unsigned i = 1;i < ndim;i++)
                    {
                        stride[i] = stride[i-1]*len[i-1];
                    }
                }

                return stride;
            }

            template <typename... Args>
            static detail::enable_if_t<sizeof...(Args) == ndim &&
                                       starts_with_len<Args...>::value,
                                       std::array<stride_type, ndim>>
            default_strides(Args... args)
            {
                return default_strides(get_len_from_args(std::forward<Args>(args)...));
            }

            template <typename... Args>
            static detail::enable_if_t<sizeof...(Args) == ndim+1 &&
                                       starts_with_len<Args...>::value &&
                                       has_layout_early<Args...>::value,
                                       std::array<stride_type, ndim>>
            default_strides(Args... args)
            {
                return default_strides(get_len_from_args(std::forward<Args>(args)...),
                                       get_layout_early_from_args(std::forward<Args>(args)...));
            }

            const_marray_view() {}

            const_marray_view(const const_marray_view& other)
            {
                reset(other);
            }

            template <typename U>
            const_marray_view(const std::array<U, ndim>& len, const_pointer ptr, Layout layout=DEFAULT)
            {
                reset(len, ptr, layout);
            }

            template <typename U, typename V>
            const_marray_view(const std::array<U, ndim>& len, const_pointer ptr, const std::array<V, ndim>& stride)
            {
                reset(len, ptr, stride);
            }

            template <typename... Args, typename=
                detail::enable_if_t<(sizeof...(Args) == ndim+1 &&
                                     starts_with_len<Args...>::value &&
                                     has_const_ptr<Args...>::value) ||
                                    (sizeof...(Args) == ndim+2 &&
                                     starts_with_len<Args...>::value &&
                                     has_const_ptr<Args...>::value &&
                                     has_layout<Args...>::value) ||
                                    (sizeof...(Args) == ndim+1+ndim &&
                                     starts_with_len<Args...>::value &&
                                     has_const_ptr<Args...>::value &&
                                     ends_with_stride<Args...>::value)>>
            const_marray_view(Args&&... args)
            {
                reset(std::forward<Args>(args)...);
            }

            template <typename U>
            const_marray_view<T, ndim> permute(const std::array<U, ndim>& perm) const
            {
                std::array<idx_type, ndim> len;
                std::array<stride_type, ndim> stride;

                for (unsigned i = 0;i < ndim;i++)
                {
                    assert(0 <= perm[i] && perm[i] < ndim);
                    for (unsigned j = 0;j < i;j++) assert(perm[i] != perm[j]);
                }

                for (unsigned i = 0;i < ndim;i++)
                {
                    len[i] = len_[perm[i]];
                    stride[i] = stride_[perm[i]];
                }

                return {len, data_, stride};
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim &&
                                detail::are_convertible<unsigned, Args...>::value,
                                const_marray_view<T, ndim>>
            permute(Args&&... args) const
            {
                return permute(make_array((unsigned)std::forward<Args>(args)...));
            }

            template <typename U, unsigned newdim>
            const_marray_view<T, newdim+1> lower(const std::array<U, newdim>& split) const
            {
                assert(newdim < ndim);

                for (unsigned i = 0;i < newdim;i++)
                {
                    assert(split[i] <= ndim);
                    if (i != 0) assert(split[i-1] <= split[i]);
                }

                std::array<idx_type, newdim> newlen;
                std::array<stride_type, newdim> newstride;

                for (unsigned i = 0;i <= newdim;i++)
                {
                    int begin = (i == 0 ? 0 : split[i-1]);
                    int end = (i == newdim-1 ? ndim-1 : split[i]-1);
                    if (begin > end) continue;

                    if (stride_[begin] < stride_[end])
                    {
                        newlen[i] = len_[end];
                        newstride[i] = stride_[begin];
                        for (unsigned j = begin;j < end;j++)
                        {
                            assert(stride_[j+1] == stride_[j]*len_[j]);
                            newlen[i] *= len_[j];
                        }
                    }
                    else
                    {
                        newlen[i] = len_[end];
                        newstride[i] = stride_[end];
                        for (unsigned j = begin;j < end;j++)
                        {
                            assert(stride_[j] == stride_[j+1]*len_[j+1]);
                            newlen[i] *= len_[j];
                        }
                    }
                }

                return {newlen, data_, newstride};
            }

            template <typename... Args>
            detail::enable_if_t<detail::are_convertible<unsigned, Args...>::value,
                                const_marray_view<T, sizeof...(Args)+1>>
            lower(Args&&... args) const
            {
                return lower(make_array((unsigned)std::forward<Args>(args)...));
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, const_reference>::type
            front() const
            {
                assert(len_[0] > 0);
                return data_[0];
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, const_reference>::type
            front(unsigned dim) const
            {
                assert(dim == 0);
                return front();
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, const_marray_view<T, ndim-1>>::type
            front(unsigned dim) const
            {
                assert(dim < ndim);
                assert(len_[dim] > 0);

                std::array<idx_type, ndim-1> len;
                std::array<stride_type, ndim-1> stride;

                std::copy_n(len_.begin(), dim, len.begin());
                std::copy_n(len_.begin()+dim+1, ndim-dim-1, len.begin()+dim);
                std::copy_n(stride_.begin(), dim, stride.begin());
                std::copy_n(stride_.begin()+dim+1, ndim-dim-1, stride.begin()+dim);

                return {len, data_, stride};
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, const_reference>::type
            back() const
            {
                assert(len_[0] > 0);
                return data_[(len_[0]-1)*stride_[0]];
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, const_reference>::type
            back(unsigned dim) const
            {
                assert(dim == 0);
                return back();
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, const_marray_view<T, ndim-1>>::type
            back(unsigned dim) const
            {
                const_marray_view<T, ndim-1> view = front(dim);
                view.data_ += (len_[dim]-1)*stride_[dim];
                return view;
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, const_reference>::type
            operator[](idx_type i) const
            {
                assert(i < len_[0]);
                return data_[i*stride()];
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, detail::const_marray_ref<T, ndim, 2>>::type
            operator[](idx_type i) const
            {
                assert(i < len_[0]);
                return {*this, 0, i};
            }

            template <typename I, unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, const_marray_view<T, 1>>::type
            operator[](const range_t<I>& x) const
            {
                assert(x.front() >= 0 && x.back() <= len_[0]);
                return {x.size(), data_+x.front()*stride_[0], stride_[0]};
            }

            template <typename I, unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, detail::const_marray_slice<T, ndim, 2, 1>>::type
            operator[](const range_t<I>& x) const
            {
                assert(x.front() >= 0 && x.back() <= len_[0]);
                return {*this, 0, {}, {}, x};
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, const_marray_view<T, 1>>::type
            operator[](slice::all_t x) const
            {
                return *this;
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, detail::const_marray_slice<T, ndim, 2, 1>>::type
            operator[](slice::all_t x) const
            {
                return {*this, 0, {}, {}, range(len_[0])};
            }

            template <typename Arg, typename=
                typename std::enable_if<detail::is_index_or_slice<Arg>::value>::type>
            auto operator()(Arg&& arg) const ->
            decltype((*this)[std::forward<Arg>(arg)])
            {
                return (*this)[std::forward<Arg>(arg)];
            }

            template <typename Arg, typename... Args, typename=
                typename std::enable_if<sizeof...(Args) == ndim-1 &&
                                        detail::are_indices_or_slices<Arg, Args...>::value>::type>
            auto operator()(Arg&& arg, Args&&... args) const ->
            decltype((*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...))
            {
                return (*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...);
            }

            const_pointer data() const
            {
                return data_;
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, idx_type>::type
            length() const
            {
                return len_[0];
            }

            idx_type length(unsigned dim) const
            {
                assert(dim < ndim);
                return len_[dim];
            }

            const std::array<idx_type, ndim>& lengths() const
            {
                return len_;
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, stride_type>::type
            stride() const
            {
                return stride_[0];
            }

            stride_type stride(unsigned dim) const
            {
                assert(dim < ndim);
                return stride_[dim];
            }

            const std::array<stride_type, ndim>& strides() const
            {
                return stride_;
            }

            operator const marray_view<T, ndim>&() const
            {
                return static_cast<const marray_view<T, ndim>&>(*this);
            }
    };

    template <typename T, unsigned ndim>
    class marray_view : public const_marray_view<T, ndim>
    {
        template <typename T_, unsigned ndim_> friend class const_marray_view;
        template <typename T_, unsigned ndim_> friend class marray_view;
        template <typename T_, unsigned ndim_, typename Allocator_> friend class marray;
        template <typename T_, unsigned ndim_, unsigned dim_> friend class detail::const_marray_ref;
        template <typename T_, unsigned ndim_, unsigned dim_> friend class detail::marray_ref;
        template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class detail::const_marray_slice;
        template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class detail::marray_slice;

        protected:
            typedef const_marray_view<T, ndim> base;
            typedef const_marray_view<T, ndim> parent;

        public:
            using typename base::idx_type;
            using typename base::size_type;
            using typename base::stride_type;
            using typename base::value_type;
            using typename base::pointer;
            using typename base::const_pointer;
            using typename base::reference;
            using typename base::const_reference;

        protected:
            using base::data_;
            using base::len_;
            using base::stride_;

            using base::reset;

            marray_view(const parent& other)
            : parent(other) {}

            using base::get_len_from_args;
            using base::get_ptr_from_args;
            using base::get_value_from_args;
            using base::get_layout_from_args;
            using base::get_stride_from_args;

            template <typename... Args> using starts_with_len = typename base::template starts_with_len<Args...>;
            template <typename... Args> using has_const_ptr = typename base::template has_const_ptr<Args...>;
            template <typename... Args> using has_ptr = typename base::template has_ptr<Args...>;
            template <typename... Args> using has_value = typename base::template has_value<Args...>;
            template <typename... Args> using has_uninit = typename base::template has_uninit<Args...>;
            template <typename... Args> using has_layout = typename base::template has_layout<Args...>;
            template <typename... Args> using ends_with_stride = typename base::template ends_with_stride<Args...>;

        public:
            marray_view() {}

            marray_view(marray_view& other)
            : parent(other) {}

            marray_view(marray_view&& other)
            : parent(other) {}

            template <typename U>
            marray_view(const std::array<U, ndim>& len, pointer ptr, Layout layout=DEFAULT)
            {
                reset(len, ptr, layout);
            }

            template <typename U, typename V>
            marray_view(const std::array<U, ndim>& len, pointer ptr, const std::array<V, ndim>& stride)
            {
                reset(len, ptr, stride);
            }

            template <typename... Args, typename=
                detail::enable_if_t<(sizeof...(Args) == ndim+1 &&
                                     starts_with_len<Args...>::value &&
                                     has_ptr<Args...>::value) ||
                                    (sizeof...(Args) == ndim+2 &&
                                     starts_with_len<Args...>::value &&
                                     has_ptr<Args...>::value &&
                                     has_layout<Args...>::value) ||
                                    (sizeof...(Args) == ndim+1+ndim &&
                                     starts_with_len<Args...>::value &&
                                     has_ptr<Args...>::value &&
                                     ends_with_stride<Args...>::value)>>
            marray_view(Args&&... args)
            {
                reset(std::forward<Args>(args)...);
            }

            marray_view& operator=(const const_marray_view<T, ndim>& other)
            {
                copy(other, *this);
                return *this;
            }

            marray_view& operator=(const marray_view& other)
            {
                return operator=(static_cast<const const_marray_view<T, ndim>&>(other));
            }

            marray_view& operator=(const T& value)
            {
                auto it = make_iterator(len_, stride_);
                auto a_ = data_;
                while (it.next(a_)) *a_ = value;
                return *this;
            }

            using base::permute;

            template <typename U>
            marray_view<T, ndim> permute(const std::array<U, ndim>& perm)
            {
                return base::permute(perm);
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim &&
                                detail::are_convertible<unsigned, Args...>::value,
                                marray_view<T, ndim>>
            permute(Args&&... args)
            {
                return base::permute(std::forward<Args>(args)...);
            }

            using base::lower;

            template <typename U, unsigned newdim>
            marray_view<T, newdim+1> lower(const std::array<U, newdim>& split)
            {
                return base::lower(split);
            }

            template <typename... Args>
            detail::enable_if_t<detail::are_convertible<unsigned, Args...>::value,
                                marray_view<T, sizeof...(Args)+1>>
            lower(Args&&... args)
            {
                return base::lower(std::forward<Args>(args)...);
            }

            void rotate_dim(unsigned dim, stride_type shift)
            {
                assert(dim < ndim);

                idx_type n = len_[dim];
                stride_type s = stride_[dim];

                shift = shift%n;
                if (shift < 0) shift += n;

                if (shift == 0) return;

                std::array<idx_type, ndim-1> sublen;
                std::array<stride_type, ndim-1> substride;

                std::copy_n(len_.begin(), dim, sublen.begin());
                std::copy_n(len_.begin()+dim+1, ndim-dim-1, sublen.begin()+dim);

                std::copy_n(stride_.begin(), dim, substride.begin());
                std::copy_n(stride_.begin()+dim+1, ndim-dim-1, substride.begin()+dim);

                pointer p = data_;
                auto it = make_iterator(sublen, substride);
                while (it.next(p))
                {
                    pointer a = p;
                    pointer b = p+(shift-1)*s;
                    for (idx_type i = 0;i < shift/2;i++)
                    {
                        std::swap(*a, *b);
                        a += s;
                        b -= s;
                    }

                    a = p+shift*s;
                    b = p+(n-1)*s;
                    for (idx_type i = 0;i < (n-shift)/2;i++)
                    {
                        std::swap(*a, *b);
                        a += s;
                        b -= s;
                    }

                    a = p;
                    b = p+(n-1)*s;
                    for (idx_type i = 0;i < n/2;i++)
                    {
                        std::swap(*a, *b);
                        a += s;
                        b -= s;
                    }
                }
            }

            template <typename U>
            void rotate(const std::array<U, ndim>& shift)
            {
                for (unsigned dim = 0;dim < ndim;dim++)
                {
                    rotate_dim(dim, shift[dim]);
                }
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim &&
                                detail::are_convertible<stride_type, Args...>::value>
            rotate(Args&&... args)
            {
                rotate(make_array((stride_type)std::forward<Args>(args)...));
            }

            using base::front;

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, reference>::type
            front()
            {
                return const_cast<reference>(base::front());
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, reference>::type
            front(unsigned dim)
            {
                return const_cast<reference>(base::front(dim));
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, marray_view<T, ndim-1>>::type
            front(unsigned dim)
            {
                return base::front(dim);
            }

            using base::back;

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, reference>::type
            back()
            {
                return const_cast<reference>(base::back());
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, reference>::type
            back(unsigned dim)
            {
                return const_cast<reference>(base::back(dim));
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, marray_view<T, ndim-1>>::type
            back(unsigned dim)
            {
                return base::back(dim);
            }

            using base::operator[];

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, reference>::type
            operator[](idx_type i)
            {
                return const_cast<reference>(base::operator[](i));
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, detail::marray_ref<T, ndim, 2>>::type
            operator[](idx_type i)
            {
                return base::operator[](i);
            }

            template <typename I, unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, marray_view<T, 1>>::type
            operator[](const range_t<I>& x)
            {
                return base::operator[](x);
            }

            template <typename I, unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, detail::marray_slice<T, ndim, 2, 1>>::type
            operator[](const range_t<I>& x)
            {
                return base::operator[](x);
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1, marray_view<T, 1>>::type
            operator[](slice::all_t x)
            {
                return base::operator[](x);
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1, detail::marray_slice<T, ndim, 2, 1>>::type
            operator[](slice::all_t x)
            {
                return base::operator[](x);
            }

            using base::operator();

            template <typename Arg, typename=
                typename std::enable_if<detail::is_index_or_slice<Arg>::value>::type>
            auto operator()(Arg&& arg) ->
            decltype((*this)[std::forward<Arg>(arg)])
            {
                return (*this)[std::forward<Arg>(arg)];
            }

            template <typename Arg, typename... Args, typename=
                typename std::enable_if<sizeof...(Args) == ndim-1 &&
                                        detail::are_indices_or_slices<Arg, Args...>::value>::type>
            auto operator()(Arg&& arg, Args&&... args) ->
            decltype((*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...))
            {
                return (*this)[std::forward<Arg>(arg)](std::forward<Args>(args)...);
            }

            using base::data;

            pointer data()
            {
                return const_cast<pointer>(base::data());
            }
    };

    template <typename T, unsigned ndim, typename Allocator=aligned_allocator<T, MARRAY_BASE_ALIGNMENT>>
    class marray : public marray_view<T, ndim>, private Allocator
    {
        template <typename T_, unsigned ndim_> friend class const_marray_view;
        template <typename T_, unsigned ndim_> friend class marray_view;
        template <typename T_, unsigned ndim_, typename Allocator_> friend class marray;
        template <typename T_, unsigned ndim_, unsigned dim_> friend class detail::const_marray_ref;
        template <typename T_, unsigned ndim_, unsigned dim_> friend class detail::marray_ref;
        template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class detail::const_marray_slice;
        template <typename T_, unsigned ndim_, unsigned dim_, unsigned newdim_> friend class detail::marray_slice;

        protected:
            typedef const_marray_view<T, ndim> base;
            typedef marray_view<T, ndim> parent;

        public:
            using typename base::idx_type;
            using typename base::size_type;
            using typename base::stride_type;
            using typename base::value_type;
            using typename base::pointer;
            using typename base::const_pointer;
            using typename base::reference;
            using typename base::const_reference;

        protected:
            using base::data_;
            using base::len_;
            using base::stride_;
            size_t size_ = 0;
            Layout layout_ = DEFAULT;

            using base::get_len_from_args;
            using base::get_ptr_from_args;
            using base::get_value_from_args;
            using base::get_layout_from_args;
            using base::get_stride_from_args;

            template <typename... Args> using starts_with_len = typename base::template starts_with_len<Args...>;
            template <typename... Args> using has_const_ptr = typename base::template has_const_ptr<Args...>;
            template <typename... Args> using has_ptr = typename base::template has_ptr<Args...>;
            template <typename... Args> using has_value = typename base::template has_value<Args...>;
            template <typename... Args> using has_uninit = typename base::template has_uninit<Args...>;
            template <typename... Args> using has_layout = typename base::template has_layout<Args...>;
            template <typename... Args> using ends_with_stride = typename base::template ends_with_stride<Args...>;

        public:
            using base::default_strides;

            marray() {}

            marray(const marray_view<T, ndim>& other, Layout layout=DEFAULT)
            {
                reset(other);
            }

            marray(const marray& other)
            {
                reset(other);
            }

            marray(marray&& other)
            {
                reset(std::move(other));
            }

            template <typename U>
            explicit marray(const std::array<U, ndim>& len, const T& val=T(), Layout layout=DEFAULT)
            {
                reset(len, val, layout);
            }

            template <typename U>
            marray(const std::array<U, ndim>& len, uninitialized_t u, Layout layout=DEFAULT)
            {
                reset(len, u, layout);
            }

            template <typename... Args, typename=
                detail::enable_if_t<(sizeof...(Args) == ndim &&
                                     starts_with_len<Args...>::value) ||
                                    (sizeof...(Args) == ndim+1 &&
                                     starts_with_len<Args...>::value &&
                                     has_value<Args...>::value) ||
                                    (sizeof...(Args) == ndim+2 &&
                                     starts_with_len<Args...>::value &&
                                     has_value<Args...>::value &&
                                     has_layout<Args...>::value) ||
                                    (sizeof...(Args) == ndim+1 &&
                                     starts_with_len<Args...>::value &&
                                     has_uninit<Args...>::value) ||
                                    (sizeof...(Args) == ndim+2 &&
                                     starts_with_len<Args...>::value &&
                                     has_uninit<Args...>::value &&
                                     has_layout<Args...>::value)>>
            marray(Args&&... args)
            {
                reset(std::forward<Args>(args)...);
            }

            ~marray()
            {
                reset();
            }

            using parent::operator=;

            marray& operator=(const marray& other)
            {
                parent::operator=(other);
                return *this;
            }

            void reset()
            {
                if (data_)
                {
                    for (size_t i = 0;i < size_;i++) data_[i].~T();
                    Allocator::deallocate(data_, size_);
                }
                size_ = 0;
                layout_ = DEFAULT;

                base::reset();
            }

            void reset(const marray_view<T, ndim>& other, Layout layout=DEFAULT)
            {
                if (std::is_scalar<T>::value)
                {
                    reset(other.len_, uninitialized, layout);
                }
                else
                {
                    reset(other.len_, T(), layout);
                }

                *this = other;
            }

            void reset(marray&& other)
            {
                swap(other);
            }

            template <typename U>
            void reset(const std::array<U, ndim>& len, const T& val=T(), Layout layout=DEFAULT)
            {
                reset(len, uninitialized, layout);
                std::uninitialized_fill_n(data_, size_, val);
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim &&
                                starts_with_len<Args...>::value>
            reset(Args&&... args)
            {
                reset(get_len_from_args(std::forward<Args>(args)...));
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim+1 &&
                                starts_with_len<Args...>::value &&
                                has_value<Args...>::value>
            reset(Args&&... args)
            {
                reset(get_len_from_args(std::forward<Args>(args)...),
                      get_value_from_args(std::forward<Args>(args)...));
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim+2 &&
                                starts_with_len<Args...>::value &&
                                has_value<Args...>::value &&
                                has_layout<Args...>::value>
            reset(Args&&... args)
            {
                reset(get_len_from_args(std::forward<Args>(args)...),
                      get_value_from_args(std::forward<Args>(args)...),
                      get_layout_from_args(std::forward<Args>(args)...));
            }

            template <typename U>
            void reset(const std::array<U, ndim>& len, uninitialized_t u, Layout layout=DEFAULT)
            {
                size_ = std::accumulate(len.begin(), len.end(), size_t(1), std::multiplies<size_t>());
                layout_ = layout;

                base::reset(len, Allocator::allocate(size_), base::default_strides(len, layout));
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim+1 &&
                                starts_with_len<Args...>::value &&
                                has_uninit<Args...>::value>
            reset(Args&&... args)
            {
                reset(get_len_from_args(std::forward<Args>(args)...),
                      uninitialized);
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim+2 &&
                                starts_with_len<Args...>::value &&
                                has_uninit<Args...>::value &&
                                has_layout<Args...>::value>
            reset(Args&&... args)
            {
                reset(get_len_from_args(std::forward<Args>(args)...),
                      uninitialized,
                      get_layout_from_args(std::forward<Args>(args)...));
            }

            template <typename U>
            void resize(const std::array<U, ndim>& len, const T& val=T())
            {
                marray a(std::move(*this));
                reset(len, val, layout_);
                marray_view<T, ndim> b(*this);

                /*
                 * It is OK to change the geometry of 'a' even if it is not
                 * a view since it is about to go out of scope.
                 */
                for (unsigned i = 0;i < ndim;i++)
                {
                    a.len_[i] = b.len_[i] = std::min(a.len_[i], b.len_[i]);
                }

                copy(a, b);
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim &&
                                starts_with_len<Args...>::value>
            resize(Args&&... args)
            {
                resize(get_len_from_args(std::forward<Args>(args)...));
            }

            template <typename... Args>
            detail::enable_if_t<sizeof...(Args) == ndim+1 &&
                                starts_with_len<Args...>::value &&
                                has_value<Args...>::value>
            resize(Args&&... args)
            {
                resize(get_len_from_args(std::forward<Args>(args)...),
                       get_value_from_args(std::forward<Args>(args)...));
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1>::type
            push_back(const T& x)
            {
                resize(len_[0]+1);
                back() = x;
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1>::type
            push_back(unsigned dim, const T& x)
            {
                assert(dim == 0);
                push_back(x);
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1>::type
            pop_back()
            {
                resize(len_[0]-1);
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_==1>::type
            pop_back(unsigned dim)
            {
                assert(dim == 0);
                pop_back();
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1>::type
            push_back(unsigned dim, const marray_view<T, ndim_-1>& x)
            {
                assert(dim < ndim);

                for (unsigned i = 0, j = 0;i < ndim;i++)
                {
                    if (i != dim)
                    {
                        assert(len_[i] == x.len_[j++]);
                    }
                }

                std::array<idx_type, ndim> len = len_;
                len[dim]++;
                resize(len);
                back(dim) = x;
            }

            template <unsigned ndim_=ndim>
            typename std::enable_if<ndim_!=1>::type
            pop_back(unsigned dim)
            {
                assert(dim < ndim);
                assert(len_[dim] > 0);

                std::array<idx_type, ndim> len = len_;
                len[dim]--;
                resize(len);
            }

            using base::permute;
            using parent::permute;

            using base::lower;
            using parent::lower;

            using parent::rotate_dim;
            using parent::rotate;

            using base::front;
            using parent::front;

            using base::back;
            using parent::back;

            using base::operator[];
            using parent::operator[];

            using base::operator();
            using parent::operator();

            using base::data;
            using parent::data;

            using base::length;
            using parent::length;

            using base::lengths;
            using parent::lengths;

            using base::stride;
            using parent::stride;

            using base::strides;
            using parent::strides;

            void swap(marray& other)
            {
                using std::swap;
                swap(data_,   other.data_);
                swap(len_,    other.len_);
                swap(stride_, other.stride_);
                swap(size_,   other.size_);
                swap(layout_, other.layout_);
            }

            friend void swap(marray& a, marray& b)
            {
                a.swap(b);
            }
    };

    /*
     * Convenient names for 1- and 2-dimensional array types.
     */
    template <typename T> using const_row_view = const_marray_view<T, 1>;
    template <typename T> using row_view = marray_view<T, 1>;
    template <typename T, typename Allocator=aligned_allocator<T, MARRAY_BASE_ALIGNMENT>> using row = marray<T, 1, Allocator>;

    template <typename T> using const_matrix_view = const_marray_view<T, 2>;
    template <typename T> using matrix_view = marray_view<T, 2>;
    template <typename T, typename Allocator=aligned_allocator<T, MARRAY_BASE_ALIGNMENT>> using matrix = marray<T, 2, Allocator>;

    template <typename T, unsigned ndim>
    void copy(const const_marray_view<T, ndim>& a, marray_view<T, ndim>& b)
    {
        assert(a.lengths() == b.lengths());

        auto it = make_iterator(a.lengths(), a.strides(), b.strides());
        auto a_ = a.data();
        auto b_ = b.data();
        while (it.next(a_, b_)) *b_ = *a_;
    }

    template <typename T, unsigned ndim>
    void copy(const const_marray_view<T, ndim>& a, marray_view<T, ndim>&& b)
    {
        copy(a, b);
    }

    template <typename T>
    matrix_view<T> operator^(matrix_view<T>& m, transpose_t t)
    {
        return matrix_view<T>(m.length(1), m.length(0), m.data(),
                              m.stride(1), m.stride(0));
    }

    template <typename T>
    matrix_view<T> operator^(matrix_view<T>&& m, transpose_t t)
    {
        return m^transpose::T;
    }

    template <typename T>
    const_matrix_view<T> operator^(const matrix_view<T>& m, transpose_t t)
    {
        return const_cast<matrix_view<T>&>(m)^transpose::T;
    }

    inline
    void gemm(int m, int n, int k,
              float alpha, const float* a, int lda,
                           const float* b, int ldb,
              float  beta,       float* c, int ldc)
    {
        sgemm_("N", "N", &m, &n, &k,
               &alpha, a, &lda,
                       b, &ldb,
                &beta, c, &ldc);
    }

    inline
    void gemm(int m, int n, int k,
              double alpha, const double* a, int lda,
                            const double* b, int ldb,
              double  beta,       double* c, int ldc)
    {
        dgemm_("N", "N", &m, &n, &k,
               &alpha, a, &lda,
                       b, &ldb,
                &beta, c, &ldc);
    }

    template <typename U>
    void gemm(const U& alpha, const const_matrix_view<U>& a,
                              const const_matrix_view<U>& b,
              const U&  beta,             matrix_view<U>& c)
    {
        using transpose::T;

        assert(a.stride(0) == 1 || a.stride(1) == 1);
        assert(b.stride(0) == 1 || b.stride(1) == 1);
        assert(c.stride(0) == 1 || c.stride(1) == 1);

        bool transc =  c.stride(1) == 1;
        bool transa = (a.stride(1) == 1) != transc;
        bool transb = (b.stride(1) == 1) != transc;

        const_matrix_view<U> at = (transa ? a^T : a);
        const_matrix_view<U> bt = (transb ? b^T : b);
              matrix_view<U> ct = (transc ? c^T : c);

        assert(at.length(0) == ct.length(0));
        assert(bt.length(1) == ct.length(1));
        assert(at.length(1) == bt.length(0));

        gemm(ct.length(0), ct.length(1), at.length(1),
             alpha, at.data(), at.stride(1),
                    bt.data(), bt.stride(1),
              beta, ct.data(), ct.stride(1));
    }

    template <typename T>
    void gemm(const T& alpha, const const_matrix_view<T>&  a,
                              const const_matrix_view<T>&  b,
              const T&  beta,             matrix_view<T>&& c)
    {
        gemm(alpha, a, b, beta, c);
    }

    template <typename U>
    void gemm(char transa, char transb,
              const U& alpha, const const_matrix_view<U>& a,
                              const const_matrix_view<U>& b,
              const U&  beta,             matrix_view<U>& c)
    {
        using transpose::T;

        if (toupper(transa) == 'T')
        {
            if (toupper(transb) == 'T') gemm(alpha, a^T, b^T, beta, c);
            else                        gemm(alpha, a^T, b  , beta, c);
        }
        else
        {
            if (toupper(transb) == 'T') gemm(alpha, a  , b^T, beta, c);
            else                        gemm(alpha, a  , b  , beta, c);
        }
    }

    template <typename T>
    void gemm(char transa, char transb,
              const T& alpha, const matrix_view<T>&  a,
                              const matrix_view<T>&  b,
              const T&  beta,       matrix_view<T>&& c)
    {
        gemm(transa, transb, alpha, a, b, beta, c);
    }
}

#endif
