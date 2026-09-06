#include "ex_common.hpp"
int main() {
  using namespace costgovernor;
  auto g = ex::gov();
  ex::accel_price(g, 1000);
  CostIntervention i;
  i.kind = InterventionKind::SELECT_CHEAPER_PLAN; i.detail = "select cheaper plan";
  std::cout << "propose: " << status_name(g->propose_intervention(i)) << "\n";
  std::cout << "authorize: " << status_name(g->authorize_intervention(i.id, i.generation)) << "\n";
  std::cout << "acknowledge: " << status_name(g->acknowledge_intervention(i.id, i.generation)) << "\n";
  CostEvidence post;
  post.id = EvidenceId(1); post.request = RequestId(1); post.epoch = CoordinatorEpoch(1); post.boot = WorkerBootId(1);
  post.accelerator_time = AcceleratorNanoseconds(1'000'000'000);
  std::vector<CostEvidence> evs{post};
  VerificationOutcome v = g->verify_intervention(i.id, i.generation, evs);
  std::cout << "verify: " << verification_outcome_name(v) << "\n";
  auto list = g->interventions();
  std::cout << "intervention lifecycle: " << intervention_lifecycle_name(list.front().lifecycle) << "\n";
  return 0;
}
