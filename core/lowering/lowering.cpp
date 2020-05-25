#include "torch/csrc/jit/passes/dead_code_elimination.h"
#include "torch/csrc/jit/passes/fuse_linear.h"
#include "torch/csrc/jit/passes/freeze_module.h"
#include "torch/csrc/jit/passes/lower_graph.h"
#include "torch/csrc/jit/passes/lower_tuples.h"
#include "torch/csrc/jit/passes/quantization.h"
#include "torch/csrc/jit/passes/guard_elimination.h"

#include "core/util/prelude.h"
#include "core/lowering/lowering.h"
#include "core/lowering/passes/passes.h"

namespace trtorch {
namespace core {
namespace lowering {

void DropUnusedNodes(torch::jit::Block* b);

void LowerBlock(torch::jit::Block* b) {
    DropUnusedNodes(b);
}

void LowerGraph(std::shared_ptr<torch::jit::Graph>& g) {
    torch::jit::EliminateRedundantGuards(g);
    passes::EliminateExceptionOrPassPattern(g);
    torch::jit::FuseLinear(g);
    torch::jit::LowerAllTuples(g);
    passes::RemoveContiguous(g);
    passes::RemoveDropout(g);
    passes::FuseFlattenLinear(g);
    passes::Conv2DToConvolution(g);
    passes::UnpackAddMM(g);
    //passes::UnpackBatchNorm(g);
    passes::UnpackLogSoftmax(g);
    //passes::RemoveDimExeception(g);
    //irfusers::UnpackBatchNorm(g);
    torch::jit::EliminateDeadCode(g);
    LOG_GRAPH(*g);
}

torch::jit::Module LowerModule(const torch::jit::script::Module& mod) {
    auto mod_ = torch::jit::freeze_module(mod);
    return mod_;
}

std::pair<std::shared_ptr<torch::jit::Graph>, std::vector<at::Tensor>> Lower(const torch::jit::script::Module& mod,
                                                                            std::string method_name) {
    auto lowered_mod = LowerModule(mod);
    auto g = lowered_mod.get_method(method_name).graph();
    LOG_GRAPH(*g);

    // Go through TRTorch Lowering to reformat graph to be conversion friendly
    // and also segment for accelerators and executors (TRT-DLA, TRT-GPU, PYT)
    LOG_GRAPH("TRTorch Graph Lowering");
    lowering::LowerGraph(g);
    //=[torch::jit::FoldConvBatchNorm2d(lowered_mod);
    LOG_GRAPH("LibTorch Lowering");
    auto graph_and_parameters = torch::jit::LowerGraph(*g, lowered_mod._ivalue());
    // Is this necessary?
    lowering::LowerBlock(g->block());
    return graph_and_parameters;
}


} // namespace lowering
} // namespace core
} // namespace trtorch
