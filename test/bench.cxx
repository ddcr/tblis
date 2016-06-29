#include <cstdlib>
#include <algorithm>
#include <limits>
#include <stdint.h>
#include <iostream>
#include <random>
#include <numeric>
#include <getopt.h>
#include <sstream>
#include <iomanip>
#include <functional>

#include "../src/util/tblis_util.hpp"
#include "tblis.hpp"

using namespace std;
using namespace blis;
using namespace tblis;
using namespace tblis::impl;
using namespace tblis::util;
using namespace tblis::blis_like;

namespace tblis
{
namespace util
{
mt19937 engine;
}
}

template <dim_t Rsub=1>
double RunKernel(dim_t R, const function<void()>& kernel)
{
    double bias = numeric_limits<double>::max();
    for (dim_t r = 0;r < R;r++)
    {
        double t0 = bli_clock();
        double t1 = bli_clock();
        bias = min(bias, t1-t0);
    }

    double dt = numeric_limits<double>::max();
    for (dim_t r = 0;r < R;r++)
    {
        double t0 = bli_clock();
        for (dim_t rs = 0;rs < Rsub;rs++) kernel();
        double t1 = bli_clock();
        dt = min(dt, t1-t0);
    }

    return dt-bias;
}

void RunExperiment(dim_t m0, dim_t m1, dim_t m_step,
                   dim_t n0, dim_t n1, dim_t n_step,
                   dim_t k0, dim_t k1, dim_t k_step,
                   const function<void(dim_t,dim_t,dim_t)>& experiment)
{
    for (dim_t m = m0, n = n0, k = k0;
         m >= min(m0,m1) && m <= max(m0,m1) &&
         n >= min(n0,n1) && n <= max(n0,n1) &&
         k >= min(k0,k1) && k <= max(k0,k1);
         m += m_step, n += n_step, k += k_step)
    {
        experiment(m, n, k);
    }
}

template <typename T>
void Benchmark(gint_t R)
{
    using namespace std::placeholders;

    FILE *mout, *tout;

    auto rand_experiment =
    [&](dim_t m, dim_t n, dim_t k, const std::function<double(double,dim_t,dim_t,dim_t)>& eff_dim)
    {
        printf("%ld %ld %ld\n", m, n, k);

        T alpha = 1.0;
        T beta = 0.0;

        {
            Matrix<T> A(m, k);
            Matrix<T> B(k, n);
            Matrix<T> C(m, n);
            A = 0.0;
            B = 0.0;
            C = 0.0;

            Scalar<T> alp(alpha);
            Scalar<T> bet(beta);

            double gflops = 2*m*n*k*1e-9;
            double dt_1 = RunKernel(R, [&]{ bli_gemm(alp, A, B, bet, C); });
            double dt_2 = RunKernel(R,
            [&]
            {
                Matrix<T> A2(k, m);
                Matrix<T> B2(k, n);
                Matrix<T> C2(m, n);
                A2.transpose();
                tblis_copyv(false, m*k, A.data(), 1, A2.data(), 1);
                tblis_copyv(false, k*n, B.data(), 1, B2.data(), 1);
                bli_gemm(alp, A2, B2, bet, C2);
                tblis_xpbyv(false, m*n, C2.data(), 1, beta, C.data(), 1);
            });
            fprintf(mout, "%e %e %e\n", eff_dim(gflops,m,n,k), gflops/dt_1, gflops/dt_2);
        }

        for (int i = 0;i < 3;i++)
        {
            vector<dim_t> len_m = RandomProductConstrainedSequence<dim_t, ROUND_NEAREST>(RandomInteger(1, 3), m);
            vector<dim_t> len_n = RandomProductConstrainedSequence<dim_t, ROUND_NEAREST>(RandomInteger(1, 3), n);
            vector<dim_t> len_k = RandomProductConstrainedSequence<dim_t, ROUND_NEAREST>(RandomInteger(1, 3), k);

            string idx_A, idx_B, idx_C;
            vector<dim_t> len_A, len_B, len_C;
            char idx = 'a';

            dim_t tm = 1;
            for (dim_t len : len_m)
            {
                idx_A.push_back(idx);
                len_A.push_back(len);
                idx_C.push_back(idx);
                len_C.push_back(len);
                idx++;
                tm *= len;
            }

            dim_t tn = 1;
            for (dim_t len : len_n)
            {
                idx_B.push_back(idx);
                len_B.push_back(len);
                idx_C.push_back(idx);
                len_C.push_back(len);
                idx++;
                tn *= len;
            }

            dim_t tk = 1;
            for (dim_t len : len_k)
            {
                idx_A.push_back(idx);
                len_A.push_back(len);
                idx_B.push_back(idx);
                len_B.push_back(len);
                idx++;
                tk *= len;
            }

            vector<int> reorder_A = range<int>(len_A.size());
            vector<int> reorder_B = range<int>(len_B.size());
            vector<int> reorder_C = range<int>(len_C.size());

            random_shuffle(reorder_A.begin(), reorder_A.end());
            random_shuffle(reorder_B.begin(), reorder_B.end());
            random_shuffle(reorder_C.begin(), reorder_C.end());

            idx_A = permute(idx_A, reorder_A);
            len_A = permute(len_A, reorder_A);
            idx_B = permute(idx_B, reorder_B);
            len_B = permute(len_B, reorder_B);
            idx_C = permute(idx_C, reorder_C);
            len_C = permute(len_C, reorder_C);

            Tensor<T> A(len_A.size(), len_A);
            Tensor<T> B(len_B.size(), len_B);
            Tensor<T> C(len_C.size(), len_C);

            len_A.resize(6);
            len_B.resize(6);
            len_C.resize(6);

            double gflops = 2*tm*tn*tk*1e-9;
            impl_type = BLAS_BASED;
            double dt_blas = RunKernel(R, [&]{ tensor_contract(alpha, A, idx_A, B, idx_B, beta, C, idx_C); });
            impl_type = BLIS_BASED;
            double dt_blis = RunKernel(R, [&]{ tensor_contract(alpha, A, idx_A, B, idx_B, beta, C, idx_C); });
            fprintf(tout, "%e %e %e - %s %ld %ld %ld %ld %ld %ld - %s %ld %ld %ld %ld %ld %ld - %s %ld %ld %ld %ld %ld %ld\n",
                    eff_dim(gflops,m,n,k), gflops/dt_blas, gflops/dt_blis,
                    idx_A.c_str(), len_A[0], len_A[1], len_A[2], len_A[3], len_A[4], len_A[5],
                    idx_B.c_str(), len_B[0], len_B[1], len_B[2], len_B[3], len_B[4], len_B[5],
                    idx_C.c_str(), len_C[0], len_C[1], len_C[2], len_C[3], len_C[4], len_C[5]);
        }
    };

    auto reg_experiment =
    [&](dim_t m, dim_t n, dim_t k, const std::function<double(double,dim_t,dim_t,dim_t)>& eff_dim)
    {
        printf("%ld %ld %ld\n", m, n, k);

        T alpha = 1.0;
        T beta = 0.0;

        {
            Matrix<T> A(m, k);
            Matrix<T> B(k, n);
            Matrix<T> C(m, n);
            A = 0.0;
            B = 0.0;
            C = 0.0;

            Scalar<T> alp(alpha);
            Scalar<T> bet(beta);

            double gflops = 2*m*n*k*1e-9;
            double dt_1 = RunKernel(R, [&]{ bli_gemm(alp, A, B, bet, C); });
            double dt_2 = RunKernel(R,
            [&]
            {
                Matrix<T> A2(k, m);
                Matrix<T> B2(k, n);
                Matrix<T> C2(m, n);
                A2.transpose();
                tblis_copyv(false, m*k, A.data(), 1, A2.data(), 1);
                tblis_copyv(false, k*n, B.data(), 1, B2.data(), 1);
                bli_gemm(alp, A2, B2, bet, C2);
                tblis_xpbyv(false, m*n, C2.data(), 1, beta, C.data(), 1);
            });
            fprintf(mout, "%e %e %e\n", eff_dim(gflops,m,n,k), gflops/dt_1, gflops/dt_2);
        }

        {
            Tensor<T> A(4, vector<dim_t>{m/10, k/10, 10, 10});
            Tensor<T> B(4, vector<dim_t>{k/10, n/10, 10, 10});
            Tensor<T> C(4, vector<dim_t>{m/10, n/10, 10, 10});

            double gflops = 2*m*n*k*1e-9;
            impl_type = BLAS_BASED;
            double dt_blas = RunKernel(R, [&]{ tensor_contract(alpha, A, "aebf", B, "ecfd", beta, C, "acbd"); });
            impl_type = BLIS_BASED;
            double dt_blis = RunKernel(R, [&]{ tensor_contract(alpha, A, "aebf", B, "ecfd", beta, C, "acbd"); });
            fprintf(tout, "%e %e %e\n", eff_dim(gflops,m,n,k), gflops/dt_blas, gflops/dt_blis);
        }
    };

    if (1)
    {
        mout = fopen("out.mat.reg.square", "w");
        tout = fopen("out.tensor.reg.square", "w");

        auto square_exp = bind(reg_experiment, _1, _2, _3,
                               [](double gflops, dim_t m, dim_t n, dim_t k)
                               { return pow(gflops/2e-9, 1.0/3.0); });
        RunExperiment(20, 1000, 20,
                      20, 1000, 20,
                      20, 1000, 20,
                      square_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.reg.rankk", "w");
        tout = fopen("out.tensor.reg.rankk", "w");

        auto rankk_exp = bind(reg_experiment, _1, _2, _3,
                              [](double gflops, dim_t m, dim_t n, dim_t k)
                              { return gflops/2e-9/m/n; });
        RunExperiment(1000, 1000,  0,
                      1000, 1000,  0,
                        20, 1000, 20,
                      rankk_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.reg.pp", "w");
        tout = fopen("out.tensor.reg.pp", "w");

        auto pp_exp = bind(reg_experiment, _1, _2, _3,
                           [](double gflops, dim_t m, dim_t n, dim_t k)
                           { return sqrt(gflops/2e-9/k); });
        RunExperiment( 20, 1000, 20,
                       20, 1000, 20,
                      250,  250,  0,
                      pp_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.reg.bp", "w");
        tout = fopen("out.tensor.reg.bp", "w");

        auto bp_exp = bind(reg_experiment, _1, _2, _3,
                           [](double gflops, dim_t m, dim_t n, dim_t k)
                           { return gflops/2e-9/m/k; });
        RunExperiment( 90,   90,  0,
                       20, 2000, 20,
                      250,  250,  0,
                      bp_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.rand.square", "w");
        tout = fopen("out.tensor.rand.square", "w");

        auto square_exp = bind(rand_experiment, _1, _2, _3,
                               [](double gflops, dim_t m, dim_t n, dim_t k)
                               { return pow(gflops/2e-9, 1.0/3.0); });
        RunExperiment(20, 1000, 20,
                      20, 1000, 20,
                      20, 1000, 20,
                      square_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.rand.rankk", "w");
        tout = fopen("out.tensor.rand.rankk", "w");

        auto rankk_exp = bind(rand_experiment, _1, _2, _3,
                              [](double gflops, dim_t m, dim_t n, dim_t k)
                              { return gflops/2e-9/m/n; });
        RunExperiment(1000, 1000,  0,
                      1000, 1000,  0,
                        20, 1000, 20,
                      rankk_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.rand.pp", "w");
        tout = fopen("out.tensor.rand.pp", "w");

        auto pp_exp = bind(rand_experiment, _1, _2, _3,
                           [](double gflops, dim_t m, dim_t n, dim_t k)
                           { return sqrt(gflops/2e-9/k); });
        RunExperiment( 20, 1000, 20,
                       20, 1000, 20,
                      256,  256,  0,
                      pp_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.rand.bp", "w");
        tout = fopen("out.tensor.rand.bp", "w");

        auto bp_exp = bind(rand_experiment, _1, _2, _3,
                           [](double gflops, dim_t m, dim_t n, dim_t k)
                           { return gflops/2e-9/m/k; });
        RunExperiment( 96,   96,  0,
                       20, 2000, 20,
                      256,  256,  0,
                      bp_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.reg.square", "w");
        tout = fopen("out.tensor.reg.square", "w");

        auto square_exp = bind(reg_experiment, _1, _2, _3,
                               [](double gflops, dim_t m, dim_t n, dim_t k)
                               { return pow(gflops/2e-9, 1.0/3.0); });
        RunExperiment(20, 1000, 20,
                      20, 1000, 20,
                      20, 1000, 20,
                      square_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.reg.rankk", "w");
        tout = fopen("out.tensor.reg.rankk", "w");

        auto rankk_exp = bind(reg_experiment, _1, _2, _3,
                              [](double gflops, dim_t m, dim_t n, dim_t k)
                              { return gflops/2e-9/m/n; });
        RunExperiment(1000, 1000,  0,
                      1000, 1000,  0,
                        20, 1000, 20,
                      rankk_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.reg.pp", "w");
        tout = fopen("out.tensor.reg.pp", "w");

        auto pp_exp = bind(reg_experiment, _1, _2, _3,
                           [](double gflops, dim_t m, dim_t n, dim_t k)
                           { return sqrt(gflops/2e-9/k); });
        RunExperiment( 20, 1000, 20,
                       20, 1000, 20,
                      250,  250,  0,
                      pp_exp);

        fclose(mout);
        fclose(tout);
    }

    if (1)
    {
        mout = fopen("out.mat.reg.bp", "w");
        tout = fopen("out.tensor.reg.bp", "w");

        auto bp_exp = bind(reg_experiment, _1, _2, _3,
                           [](double gflops, dim_t m, dim_t n, dim_t k)
                           { return gflops/2e-9/m/k; });
        RunExperiment( 90,   90,  0,
                       20, 2000, 20,
                      250,  250,  0,
                      bp_exp);

        fclose(mout);
        fclose(tout);
    }
}

int main(int argc, char **argv)
{
    gint_t R = 20;
    time_t seed = time(NULL);

    tblis_init();

    struct option opts[] = {{"rep", required_argument, NULL, 'r'},
                            {"seed", required_argument, NULL, 's'},
                            {0, 0, 0, 0}};

    int arg;
    int index;
    istringstream iss;
    while ((arg = getopt_long(argc, argv, "r:s:", opts, &index)) != -1)
    {
        switch (arg)
        {
            case 'r':
                iss.str(optarg);
                iss >> R;
                break;
            case 's':
                iss.str(optarg);
                iss >> seed;
                break;
            case '?':
                abort();
                break;
        }
    }

    cout << "Using mt19937 with seed " << seed << endl;
    engine.seed(seed);

    Benchmark<double>(R);

    tblis_finalize();

    return 0;
}