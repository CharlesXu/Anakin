#include "saber/core/tensor_op.h"
#include "saber/funcs/timer.h"
#include "test/saber/test_saber_func.h"

#ifdef USE_ARM_PLACE
#include "saber/funcs/impl/arm/neon/impl/sgemm_prepacked.h"

using namespace anakin::saber;

int cluster = 0;
int threads = 1;

bool Basic_test = false;

int M = 512;
int N = 512;
int K = 512;
bool traA = false;
bool traB = false;
bool flag_relu = false;
bool flag_bias = false;
int test_iter = 1;
bool COMPARE_RESULT = true;
typedef Tensor<ARM> TensorHf4;


template  <typename type,  typename type2>
static void basic_gemm(int m, int n, int k, const type* a, const type* b, const type2* bias, type2* c, \
    type2 alpha, type2 beta, \
    bool trans_a = false, bool trans_b = false, bool flag_bias = false, bool flag_relu = false) {
#pragma omp parallel for
    for (int i = 0; i < m; ++i) {
        type2 bias_data = (type2)0;
        if (flag_bias) {
            bias_data = bias[i];
        }
        for (int j = 0; j < n; ++j) {
            type2 sum = static_cast<type2>(0);
            for (int l = 0; l < k; ++l) {
                type av;
                type bv;
                if (trans_a) {
                    av = a[l * m + i];
                } else{
                    av = a[i * k + l];
                }
                if (trans_b) {
                    bv = b[j * k + l];
                } else {
                    bv = b[l * n + j];
                }
                sum += av * bv;
            }
            type2 tmp = alpha * sum + beta * c[i * n + j] + bias_data;
            if (flag_relu) {
                c[i * n + j] = tmp > (type2)0? tmp : (type2)0;
            } else {
                c[i * n + j] = tmp;
            }
        }
    }
}


SaberStatus test_arm_sgemm(int M, int N, int K, bool tra, bool trb, bool flag_bias, bool flag_relu, int in_threads) {
    double to = 0;
    double min_time = 1000000;
    SaberTimer<ARM> t1;
    Context<ARM> ctx1;
    PowerMode mode = (PowerMode)cluster;
    ctx1.set_run_mode(mode, in_threads);
    LOG(INFO) << "CPU ARCH: A" << ctx1.get_arch();
    LOG(INFO) << "test threads activated";
#pragma omp parallel
    {
#ifdef USE_OPENMP
        int thread = omp_get_num_threads();
        LOG(INFO) << "number of threads: " << in_threads;
#endif
    }
    Shape sha({1, 1, M, K});
    Shape shb({1, 1, N, K});
    Shape shc({1, 1, M, N});
    TensorHf4 ta;
    TensorHf4 tb;
    TensorHf4 tbias;
    ta.reshape(sha);
    tb.reshape(shb);
    tbias.reshape(Shape({1, 1, 1, M}));
    fill_tensor_rand(ta, -1.f, 1.f);
//    fill_tensor_const(ta, 1.f);
    fill_tensor_rand(tb, -1.f, 1.f);
//    fill_tensor_const(tb, 1.f);
    fill_tensor_rand(tbias, -1.f, 1.f);
    TensorHf4 tout_basic;
    TensorHf4 tout_saber;
    tout_saber.reshape(shc);
    int m = M;
    int n = N;
    int k = K;
    LOG(INFO) << "sgemm M: " << m << ", N: " << n << ", K: " << k;
    LOG(INFO) << "transA: " << (tra? "true" : "false") << ", transB: " << (trb? "true" : "false");
    LOG(INFO) << "relu: " << (flag_relu? "true" : "false") << ", bias: " << (flag_bias? "true" : "false");
    LOG(INFO) << "test iter: " << test_iter;
    LOG(INFO) << "compare result with basic sgemm: " << (COMPARE_RESULT? "true" : "false");
    const float* da = static_cast<const float*>(ta.data());
    const float* db = static_cast<const float*>(tb.data());
    if (COMPARE_RESULT) {
        LOG(INFO) << "run basic conv for precision comparation";
        tout_basic.reshape(shc);
        float* dc_basic = static_cast<float*>(tout_basic.mutable_data());
        basic_gemm(m, n, k, da, db, static_cast<const float*>(tbias.data()), dc_basic, 1.f, 0.f, tra, trb, flag_bias, flag_relu);
        //print_tensor(tout_basic);
    }

    //! compute
    LOG(INFO) << "saber sgemm compute";
    to = 0;
    int lda;
    int ldb;
    int ldc;
    if (tra) {
        lda = m;
    } else {
        lda = k;
    }
    if (trb) {
        ldb = k;
    } else {
        ldb = n;
    }
    ldc = n;
    double ops = 2.0 * m * n * k;
    float* dc_saber = static_cast<float*>(tout_saber.mutable_data());

    to = 0;
    min_time = 1000000;
    int hblock = get_hblock(ctx1.get_arch());
    int round_up_a = ((hblock + m - 1) / hblock) * hblock;
    LOG(INFO) << "hblock = " << hblock << ", round up = " << round_up_a;
    TensorHf4 tpackedA(Shape({1, 1, round_up_a, K}));
    prepackA(static_cast<float*>(tpackedA.mutable_data()), da, lda, 0, m, 0, k, tra, &ctx1);
    /// warm up
    for (int i = 0; i < 5; ++i) {
        sgemm_prepack(static_cast<const float*>(tpackedA.data()), db, static_cast<const float*>(tbias.data()), \
            dc_saber, m, n, k, flag_bias, flag_relu, trb, &ctx1);
    }
    for (int i = 0; i < test_iter; ++i) {
        t1.clear();
        t1.start(ctx1);
        sgemm_prepack(static_cast<const float*>(tpackedA.data()), db, static_cast<const float*>(tbias.data()), \
            dc_saber, m, n, k, flag_bias, flag_relu, trb, &ctx1);
        t1.end(ctx1);
        to += t1.get_average_ms();
        if (t1.get_average_ms() < min_time) {
            min_time = t1.get_average_ms();
        }
    }

    float cpu_freq_cur = mode == SABER_POWER_HIGH
            ? Env<ARM>::cur_env()[0]._info._max_frequence : Env<ARM>::cur_env()[0]._info._min_frequence;
    float cpu_ca_theory = cpu_freq_cur * 8.0f / 1000;
    int th_num = threads;

    LOG(INFO) << "saber packed gemm running time, ave: " << to / test_iter << ", min time: " << min_time;
    LOG(INFO) << "calculate: OPS: " << ops << " timer: " << to / test_iter << " mean GOPS: " << 0.000001f * ops * test_iter / to
        << " GFLOPS, max gops: " << 0.000001f * ops / min_time << " GFLOPS  cpu potential: "
        << 0.000001f * ops / min_time / cpu_ca_theory / th_num * 100;
    //print_tensor(tout_saber);
    if (COMPARE_RESULT) {
        double max_ratio = 0;
        double max_diff = 0;
        tensor_cmp_host((const float*)tout_basic.data(), (const float*)tout_saber.data(),
                    tout_basic.valid_size(), max_ratio, max_diff);
        LOG(INFO) << "compare result, max diff: " << max_diff << ", max ratio: " << max_ratio;
        if (fabs(max_ratio) > 1e-4f && fabsf(max_diff) > 5e-5f) {
            LOG(INFO) << "basic result: ";
            print_tensor(tout_basic);
            LOG(INFO) << "saber result: ";
            print_tensor(tout_saber);
            return SaberInvalidValue;
        }
    }
    return SaberSuccess;
}

TEST(TestSaberFunc, test_func_sgemm_prepacked) {
    if (Basic_test) {
        LOG(INFO) << "run basic sgemm test";
        for (auto& m : {1, 8, 16, 111, 256, 397, 512, 777, 1024}) {
            for (auto& n : {1, 3, 13, 141, 256, 345, 512, 789, 1024}) {
                for (auto& k : {1, 4, 15, 59, 128, 234, 512, 678, 1024}) {
                    for (auto& tra : {false, true}) {
                        for (auto& trb : {false, true}) {
                            for (auto& flag_bias : {false, true}) {
                                for (auto& flag_relu : {false, true}) {
                                    for (auto& th : {1, 2, 4}) {
                                        SaberStatus flag = test_arm_sgemm(m, n, k, tra, trb, flag_bias, flag_relu, th);
                                        if (flag == SaberSuccess) {
                                            LOG(INFO) << "test m = " << m << ", n=" << n << ", k=" << k << \
                                                ", bias: " << (flag_bias? "true" : "false") << ", relu: " << \
                                                (flag_relu? "true" : "false") << ", trans A: " << (tra? "true" : "false") << \
                                                ", trans B: " << (trb? "true" : "false") << " passed";
                                        } else {
                                            LOG(FATAL) << "test m = " << m << ", n=" << n << ", k=" << k << \
                                                ", bias: " << (flag_bias? "true" : "false") << ", relu: " << \
                                                (flag_relu? "true" : "false") << ", trans A: " << (tra? "true" : "false") << \
                                                ", trans B: " << (trb? "true" : "false") << " failed";
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
TEST(TestSaberFunc, test_func_sgemm_prepacked_custom) {
    auto flag = test_arm_sgemm(M, N, K, traA, traB, flag_bias, flag_relu, threads);
    if (flag != SaberSuccess) {
        LOG(FATAL) << "test m = " << M << ", n=" << N << ", k=" << K << \
            ", trans A: " << traA << ", trans B: " << traB << ", bias: " << flag_bias << \
            ", relu: " << flag_relu  << " failed!!";
    }
    LOG(INFO) << "test m = " << M << ", n=" << N << ", k=" << K << \
            ", trans A: " << traA << ", trans B: " << traB << ", bias: " << flag_bias << \
            ", relu: " << flag_relu  << " passed!!";
}


int main(int argc, const char** argv){
    anakin::saber::Env<ARM>::env_init();
    LOG(ERROR) << "usage: ./" << argv[0] << " [do_basic_test] [cluster]  [threads]  [m] [n]  [k] [transA] [transB] [relu] [bias] [test iter] [compare result]";
    if (argc > 1) {
        Basic_test = atoi(argv[1]) > 0;
    }
    if (argc > 2) {
        cluster = atoi(argv[2]);
    }
    if (argc > 3) {
        threads = atoi(argv[3]);
    }
    if (argc > 4) {
        if (argc < 10) {
            LOG(ERROR) << "usage: ./" << argv[0] << " [do_basic_test] [cluster]  [threads]  [m] [n]  [k] [transA] [transB] [relu] [bias] [test iter] [compare result]";
            return 0;
        }
        M = atoi(argv[4]);
        N = atoi(argv[5]);
        K = atoi(argv[6]);
        traA = atoi(argv[7]) > 0;
        traB = atoi(argv[8]) > 0;
        flag_relu = atoi(argv[9]) > 0;
        flag_bias = atoi(argv[10]) > 0;
    }
    if (argc > 11) {
        test_iter = atoi(argv[11]);
    }
    if (argc > 12) {
        COMPARE_RESULT = atoi(argv[12]) > 0;
    }
    // initial logger
    logger::init(argv[0]);
    InitTest();
    RUN_ALL_TESTS(argv[0]);
    return 0;
}

#else

int main(int argc, const char** argv){
    LOG(INFO) << "this unit test only be used in TargetType is ARM";
    return 0;
}

#endif
