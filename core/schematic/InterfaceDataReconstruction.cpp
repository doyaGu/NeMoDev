#include "core/schematic/InterfaceDataReconstruction.h"

#include "CKAll.h"

#include "core/schematic/detail/GraphBuilder.h"
#include "core/interface/InterfaceDataValidation.h"
#include "core/schematic/LayoutCalculator.h"
#include "core/schematic/LinkRouting.h"

void ReconstructInterfaceData(
    InterfaceData &data,
    CKBehavior *script,
    const InterfaceDataReconstructionOptions &options) {
    if (options.diagnostics) *options.diagnostics = InterfaceDataReconstructionDiagnostics();
    if (!script) {
        data.Clear();
        return;
    }

    GraphBuilder graphBuilder(
        data,
        script->GetCKContext(),
        options.preserveSavedDirectParameterLinks);
    graphBuilder.BuildGraph(script);

    const std::vector<InterfaceDataValidationIssue> validationIssues =
        ValidateInterfaceData(
            data, script, InterfaceDataValidationStage::AfterBuildGraph);
    ReportInterfaceDataValidationIssues(script->GetCKContext(), validationIssues);

    LayoutCalculator layoutCalculator(data, script->GetCKContext());
    layoutCalculator.CalculateRuntimeGraphLayout(script);

    const LinkRoutingPolicy linkRoutingPolicy =
        options.linkRouting == InterfaceDataLinkRouting::OrthogonalBeautify
            ? LinkRoutingPolicy::OrthogonalBeautify
            : LinkRoutingPolicy::Straight;
    LinkRoutingDiagnostics routingDiagnostics;
    const bool routingSucceeded = ApplyLinkRoutingPolicy(
            data,
            script,
            linkRoutingPolicy,
            &routingDiagnostics);
    if (options.diagnostics) {
        options.diagnostics->linkRoutingSucceeded = routingSucceeded;
        options.diagnostics->unresolvedEndpointCount = routingDiagnostics.unresolvedEndpointCount;
        options.diagnostics->routingFailureCount = routingDiagnostics.routingFailureCount;
        options.diagnostics->blockedEndpointLinkCount = routingDiagnostics.blockedEndpointLinkCount;
    }
    if (!routingSucceeded) {
        script->GetCKContext()->OutputToConsoleEx(
            (CKSTRING)"Link routing: unresolved endpoints=%d, routing failures=%d",
            routingDiagnostics.unresolvedEndpointCount,
            routingDiagnostics.routingFailureCount);
    }

}
