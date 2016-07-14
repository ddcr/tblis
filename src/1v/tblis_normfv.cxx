#include "tblis.hpp"

namespace tblis
{

template <typename T>
void tblis_normfv_ref(ThreadCommunicator& comm,
                      idx_type n, const T* restrict A, stride_type inc_A, T& restrict norm)
{
    norm = T();

    if (n == 0) return;

    T subnorm = T();

    idx_type n_min, n_max;
    std::tie(n_min, n_max, std::ignore) = comm.distribute_over_threads(n);

    if (inc_A == 1)
    {
        for (idx_type i = n_min;i < n_max;i++)
        {
            subnorm += norm2(A[i]);
        }
    }
    else
    {
        A += n_min*inc_A;
        for (idx_type i = n_min;i < n_max;i++)
        {
            subnorm += norm2(*A);
            A += inc_A;
        }
    }

    comm.reduce(subnorm);
    norm = sqrt(real(subnorm));
}

template <typename T>
void tblis_normfv(const_row_view<T> A, T& norm)
{
    tblis_normfv(A.length(), A.data(), A.stride(), norm);
}

template <typename T>
T tblis_normfv(const_row_view<T> A)
{
    T norm;
    tblis_normfv(A, norm);
    return norm;
}

template <typename T>
void tblis_normfv(idx_type n, const T* A, stride_type inc_A, T& norm)
{
    parallelize
    (
        [&](ThreadCommunicator& comm)
        {
            tblis_normfv_ref(comm, n, A, inc_A, norm);
        }
    );
}

template <typename T>
T tblis_normfv(idx_type n, const T* A, stride_type inc_A)
{
    T norm;
    tblis_normfv(n, A, inc_A, norm);
    return norm;
}

#define INSTANTIATE_FOR_TYPE(T) \
template void tblis_normfv_ref(ThreadCommunicator& comm, idx_type n, const T* A, stride_type inc_A, T& norm); \
template void tblis_normfv(idx_type n, const T* A, stride_type inc_A, T& norm); \
template    T tblis_normfv(idx_type n, const T* A, stride_type inc_A); \
template void tblis_normfv(const_row_view<T> A, T& norm); \
template    T tblis_normfv(const_row_view<T> A);
#include "tblis_instantiate_for_types.hpp"

}
