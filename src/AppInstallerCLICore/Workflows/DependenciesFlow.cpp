// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"
#include "DependenciesFlow.h"
#include "InstallFlow.h"
#include "ManifestComparator.h"


namespace AppInstaller::CLI::Workflow
{
    using namespace AppInstaller::Repository;
    using namespace Manifest;

    void ReportDependencies::operator()(Execution::Context& context) const
    {
        if (!Settings::ExperimentalFeature::IsEnabled(Settings::ExperimentalFeature::Feature::Dependencies))
        {
            return;
        }
        auto info = context.Reporter.Info();
        
        const auto& dependencies = context.Get<Execution::Data::Dependencies>();
        if (dependencies.HasAny())
        {
            info << Resource::StringId(m_messageId) << std::endl;

            if (dependencies.HasAnyOf(DependencyType::WindowsFeature))
            {
                info << "  - " << Resource::String::WindowsFeaturesDependencies << std::endl;
                dependencies.ApplyToType(DependencyType::WindowsFeature, [&info](Dependency dependency) {info << "      " << dependency.Id << std::endl; });
            }

            if (dependencies.HasAnyOf(DependencyType::WindowsLibrary))
            {
                info << "  - " << Resource::String::WindowsLibrariesDependencies << std::endl;
                dependencies.ApplyToType(DependencyType::WindowsLibrary, [&info](Dependency dependency) {info << "      " << dependency.Id << std::endl; });
            }

            if (dependencies.HasAnyOf(DependencyType::Package))
            {
                info << "  - " << Resource::String::PackageDependencies << std::endl;
                dependencies.ApplyToType(DependencyType::Package, [&info](Dependency dependency)
                    {
                        info << "      " << dependency.Id;
                        if (dependency.MinVersion) info << " [>= " << dependency.MinVersion.value().ToString() << "]";
                        info << std::endl;
                    });
            }

            if (dependencies.HasAnyOf(DependencyType::External))
            {
                info << "  - " << Resource::String::ExternalDependencies << std::endl;
                dependencies.ApplyToType(DependencyType::External, [&info](Dependency dependency) {info << "      " << dependency.Id << std::endl; });
            }
        }
    }

    void GetInstallersDependenciesFromManifest(Execution::Context& context) {
        if (Settings::ExperimentalFeature::IsEnabled(Settings::ExperimentalFeature::Feature::Dependencies))
        {
            const auto& manifest = context.Get<Execution::Data::Manifest>();
            DependencyList allDependencies;

            for (const auto& installer : manifest.Installers)
            {
                allDependencies.Add(installer.Dependencies);
            }

            context.Add<Execution::Data::Dependencies>(allDependencies);
        }
    }

    void GetDependenciesFromInstaller(Execution::Context& context)
    {
        if (Settings::ExperimentalFeature::IsEnabled(Settings::ExperimentalFeature::Feature::Dependencies))
        {
            const auto& installer = context.Get<Execution::Data::Installer>();
            if (installer)
            {
                context.Add<Execution::Data::Dependencies>(installer->Dependencies);
            }
        }
    }

    void GetDependenciesInfoForUninstall(Execution::Context& context)
    {
        if (Settings::ExperimentalFeature::IsEnabled(Settings::ExperimentalFeature::Feature::Dependencies))
        {
            // TODO make best effort to get the correct installer information, it may be better to have a record of installations and save the correct installers
            context.Add<Execution::Data::Dependencies>(DependencyList()); // sending empty list of dependencies for now
        }
    }

    void OpenDependencySource(Execution::Context& context)
    {
        if (context.Contains(Execution::Data::PackageVersion))
        {
            const auto& packageVersion = context.Get<Execution::Data::PackageVersion>();
            context.Add<Execution::Data::DependencySource>(packageVersion->GetSource());
            /*context <<
                Workflow::OpenCompositeSource(Repository::PredefinedSource::Installed, true);*/
        }
        else
        { // install from manifest requires --dependency-source to be set
            context <<
                Workflow::OpenSource(true) <<
                Workflow::OpenCompositeSource(Repository::PredefinedSource::Installed, true);
        }
    }


    void BuildPackageDependenciesGraph(Execution::Context& context)
    {
        auto info = context.Reporter.Info();
        const auto& rootManifest = context.Get<Execution::Data::Manifest>();
        Dependency rootAsDependency = Dependency(DependencyType::Package, rootManifest.Id, rootManifest.Version);
        
        const auto& rootDependencies = context.Get<Execution::Data::Installer>()->Dependencies; 
        // installer should exist, otherwise previous workflows should have failed
        context.Add<Execution::Data::Dependencies>(rootDependencies); // to use in report
        // TODO remove this ^ if we are reporting dependencies somewhere else while installing/managing them
        
        if (rootDependencies.Empty())
        {
            // If there's no dependencies there's nothing to do aside of logging the outcome
            return;
        }

        context << OpenDependencySource;
        if (!context.Contains(Execution::Data::DependencySource))
        {
            info << "dependency source not found" << std::endl; //TODO localize message
            AICLI_TERMINATE_CONTEXT(APPINSTALLER_CLI_ERROR_INTERNAL_ERROR); // TODO create specific error code
        }

        const auto& source = context.Get<Execution::Data::DependencySource>();
        std::map<string_t, PackagesAndInstallers> dependenciesInstallers;

        DependencyGraph dependencyGraph(rootAsDependency, rootDependencies, 
            [&](Dependency node) {
                auto info = context.Reporter.Info();

                SearchRequest searchRequest;
                searchRequest.Filters.emplace_back(PackageMatchFilter(PackageMatchField::Id, MatchType::CaseInsensitive, node.Id));
                //TODO add min version filter to search request ?
                const auto& matches = source->Search(searchRequest).Matches;

                if (!matches.empty())
                {
                    if (matches.size() > 1) {
                        info << "Too many matches"; //TODO localize all errors
                        return DependencyList(); //return empty dependency list, TODO change this to actually manage errors
                    }
                    const auto& match = matches.at(0);

                    const auto& package = match.Package;
                    if (package->GetInstalledVersion() && node.IsVersionOk(package->GetInstalledVersion()->GetManifest().Version))
                    {
                        return DependencyList(); //return empty dependency list, as we won't keep searching for dependencies for installed packages
                        //TODO we should have this information on the graph, to avoid trying to install it later
                        // TODO if it's already installed we need to upgrade it
                    }
                    else
                    {
                        const auto& latestVersion = package->GetLatestAvailableVersion();
                        if (!latestVersion) {
                            info << "No package version found"; //TODO localize all errors
                            return DependencyList(); //return empty dependency list, TODO change this to actually manage errors
                        }

                        const auto& manifest = latestVersion->GetManifest();
                        if (manifest.Installers.empty()) {
                            info << "No installers found"; //TODO localize all errors
                            return DependencyList(); //return empty dependency list, TODO change this to actually manage errors
                        }

                        if (!node.IsVersionOk(manifest.Version))
                        {
                            info << "Minimum required version not available"; //TODO localize all errors
                            return DependencyList(); //return empty dependency list, TODO change this to actually manage errors
                        }

                        // TODO FIX THIS, have a better way to pick installer (other than the first one)
                        const auto* installer = &manifest.Installers.at(0);
                        // the problem is SelectInstallerFromMetadata(context, packageLatestVersion->GetMetadata()) uses context data so it ends up returning
                        // the installer for the root package being installed.
                        //const auto& installer = SelectInstallerFromMetadata(context, latestVersion->GetMetadata());

                        const auto& nodeDependencies = installer->Dependencies;
                        
                        //auto packageDescription = AppInstaller::CLI::PackageCollection::Package(manifest.Id, manifest.Version, manifest.Channel);
                        // create package description too be able to use it for installer
                        //dependenciesInstallers[node.Id] = PackagesAndInstallers(installer, latestVersion, manifest.Version, manifest.Channel);
                        return nodeDependencies;
                    }
                }
                else
                {
                    info << "No matches"; //TODO localize all errors
                    return DependencyList(); //return empty dependency list, TODO change this to actually manage errors
                }
            });
        
        dependencyGraph.BuildGraph(); // maybe it's better if it already does it on the constructor?

        if (dependencyGraph.HasLoop())
        {
            info << "has loop" << std::endl;
            Logging::Log().Write(Logging::Channel::CLI, Logging::Level::Warning, "Dependency loop found"); //TODO localization
            //TODO warn user but try to install either way
            return;
        }

        // TODO raise error for failedPackages (if there's at least one)

        const auto& installationOrder = dependencyGraph.GetInstallationOrder();

        
        std::vector<PackagesAndInstallers> installers;

        info << "order: "; //-- only for debugging
        for (auto const& node : installationOrder)
        {
            info << node.Id << ", "; //-- only for debugging
            installers.push_back(dependenciesInstallers.find(node.Id)->second);
        }
        info << std::endl; //-- only for debugging

        bool allSucceeded = InstallPackages(context, installers);
        if (!allSucceeded)
        {
            context.Reporter.Error() << "error installing dependencies" << std::endl; //TODO localize error
            AICLI_TERMINATE_CONTEXT(APPINSTALLER_CLI_ERROR_INTERNAL_ERROR); // TODO create specific error code
        }
    }
}