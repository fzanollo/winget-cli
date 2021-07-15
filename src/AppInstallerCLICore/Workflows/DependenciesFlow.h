// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "ExecutionContext.h"

namespace AppInstaller::CLI::Workflow
{
    // Shows information about dependencies.
    // Required Args: message to use at the beginning, before outputting dependencies
    // Inputs: Dependencies
    // Outputs: None
    struct ReportDependencies : public WorkflowTask
    {
        ReportDependencies(AppInstaller::StringResource::StringId messageId) :
            WorkflowTask("ReportDependencies"), m_messageId(messageId) {}

        void operator()(Execution::Context& context) const override;

    private:
        AppInstaller::StringResource::StringId m_messageId;
    };

    // Gathers all installers dependencies from manifest.
    // Required Args: None
    // Inputs: Manifest
    // Outputs: Dependencies
    void GetInstallersDependenciesFromManifest(Execution::Context& context);

    // Gathers package dependencies information from installer.
    // Required Args: None
    // Inputs: Installer
    // Outputs: Dependencies
    void GetDependenciesFromInstaller(Execution::Context& context);

    // TODO: 
    // Gathers dependencies information for the uninstall command.
    // Required Args: None
    // Inputs: None
    // Outputs: Dependencies
    void GetDependenciesInfoForUninstall(Execution::Context& context);

    // Builds the dependency graph.
    // Required Args: None
    // Inputs: DependencySource
    // Outputs: Dependencies
    void BuildPackageDependenciesGraph(Execution::Context& context);

    // Sets up the source used to get the dependencies.
    // Required Args: None
    // Inputs: PackageVersion, Manifest
    // Outputs: DependencySource
    void OpenDependencySource(Execution::Context& context);

    bool graphHasLoop(const std::map<AppInstaller::Manifest::Dependency, std::vector<AppInstaller::Manifest::Dependency>>& dependencyGraph, 
        const AppInstaller::Manifest::Dependency& root,
        std::vector<AppInstaller::Manifest::Dependency>& order);
    bool hasLoopDFS(std::set<AppInstaller::Manifest::Dependency> visited,
        const AppInstaller::Manifest::Dependency& node,
        const std::map<AppInstaller::Manifest::Dependency, std::vector<AppInstaller::Manifest::Dependency>>& dependencyGraph,
        std::vector<AppInstaller::Manifest::Dependency>& order);
}