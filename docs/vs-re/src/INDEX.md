# Codigo-fonte do UI do Visual Studio 2026 (decompilado)

Fonte: instalacao local, Community 2026 18.10.12224.181. Decompilado com ilspycmd 11.2
(ICSharpCode.Decompiler). BAML virou XAML; code-behind virou C#.

Corpus em disco: `C:/Users/pedri/Downloads/vs-ui-src/<fatia>/<assembly>/`

## Totais

| fatia | assemblies | .cs | .xaml | MB |
|---|---|---|---|---|
| commonext | 64 | 14054 | 577 | 60.3 |
| extensions | 1042 | 163511 | 1534 | 687.1 |
| shell | 532 | 81196 | 766 | 282.7 |
| theme | 1 | 3047 | 286 | 14.7 |
| winforms | 137 | 30636 | 682 | 133.8 |
| **total** | **1776** | **292444** | **3845** | **1178.6** |

## fatia commonext

| # | assembly | .cs | .xaml | MB |
|---|---|---|---|---|
| 1 | VsDebugPresentationPackage | 1187 | 71 | 5.9 |
| 2 | Microsoft.VisualStudio.Platform.VSEditor | 1585 | 10 | 5.5 |
| 3 | Microsoft.VisualStudio.Editor.Implementation | 1148 | 31 | 5.4 |
| 4 | Microsoft.TeamFoundation.VersionControl.Controls | 663 | 26 | 4.6 |
| 5 | Microsoft.VisualStudio.ProjectSystem.VS.Implementation | 758 | 18 | 4.1 |
| 6 | Microsoft.TeamFoundation.Git.Controls | 559 | 57 | 3.2 |
| 7 | Microsoft.VisualStudio.LanguageServer.Client.Implementation | 902 | 3 | 2.9 |
| 8 | Microsoft.TeamFoundation.Git.Provider | 476 | 34 | 2.6 |
| 9 | Microsoft.DiagnosticsHub.VisualStudio.Package | 567 | 14 | 2.5 |
| 10 | Microsoft.VisualStudio.TeamFoundation.VersionControl | 389 | 13 | 2.2 |
| 11 | Microsoft.TeamFoundation.WorkItemTracking.Controls | 348 | 14 | 2.0 |
| 12 | Microsoft.TeamFoundation.Controls | 488 | 26 | 2.0 |
| 13 | Microsoft.TeamFoundation.Build.Controls | 331 | 17 | 1.6 |
| 14 | Microsoft.ML.ModelBuilder | 290 | 66 | 1.6 |
| 15 | Microsoft.VisualStudio.TeamFoundation | 267 | 16 | 1.3 |
| 16 | Microsoft.VisualStudio.TeamFoundation.WorkItemTracking | 225 | 12 | 1.0 |
| 17 | Microsoft.DiagnosticsHub.InstrumentationAnalyzer | 57 | 0 | 0.9 |
| 18 | NuGet.PackageManagement.UI | 205 | 35 | 0.9 |
| 19 | Microsoft.DiagnosticsHub.CpuSamplingAnalyzer | 74 | 0 | 0.9 |
| 20 | Microsoft.TeamFoundation.VersionControl.UIFeatures | 119 | 8 | 0.8 |
| 21 | NuGet.PackageManagement.VisualStudio | 207 | 0 | 0.7 |
| 22 | Microsoft.TeamFoundation.CodeReview.Controls | 140 | 12 | 0.6 |
| 23 | VSDebugCoreUI | 136 | 12 | 0.6 |
| 24 | Microsoft.DiagnosticsHub.Instrumentation | 170 | 22 | 0.5 |
| 25 | Microsoft.DiagnosticsHub.CpuSampling | 140 | 11 | 0.5 |

## fatia extensions

| # | assembly | .cs | .xaml | MB |
|---|---|---|---|---|
| 1 | Microsoft.SqlServer.TransactSql.ScriptDom | 1615 | 0 | 20.1 |
| 2 | Microsoft.CodeAnalysis.CSharp__e6efc55e | 1703 | 0 | 19.6 |
| 3 | Microsoft.Data.Tools.Schema.Sql | 3573 | 0 | 16.7 |
| 4 | Microsoft.CodeAnalysis.CSharp | 1716 | 0 | 16.2 |
| 5 | Microsoft.CodeAnalysis.VisualBasic__f4a89ed0 | 1452 | 0 | 15.6 |
| 6 | GitHub.Copilot.SDK | 1549 | 0 | 13.0 |
| 7 | GitHub.Copilot.SDK__a3469ddf | 1514 | 0 | 12.8 |
| 8 | Microsoft.CodeAnalysis.VisualBasic | 1452 | 0 | 12.7 |
| 9 | Microsoft.CodeAnalysis.CSharp.Scope | 1594 | 0 | 12.7 |
| 10 | Microsoft.WindowsAzure.HDIStudio.VsExtension.HQLLanguageService | 146 | 0 | 11.7 |
| 11 | ScopeCompiler | 1824 | 0 | 11.6 |
| 12 | Microsoft.CodeAnalysis.CSharp__08ae9439 | 1449 | 0 | 11.2 |
| 13 | ScopeCompiler__260e12ab | 1768 | 0 | 11.1 |
| 14 | Microsoft.VisualStudio.Copilot.Core__8fba5bf4 | 1904 | 0 | 10.8 |
| 15 | Azure.ResourceManager.Sql | 1066 | 0 | 10.6 |
| 16 | Microsoft.SqlServer.TransactSql.ScriptDom.Nrt | 1289 | 0 | 10.3 |
| 17 | Microsoft.VisualStudio.Copilot.Core | 1937 | 0 | 9.9 |
| 18 | System.Management.Automation | 1817 | 0 | 9.0 |
| 19 | Microsoft.CodeAnalysis.CSharp.Scope__faa771c3 | 1169 | 0 | 8.9 |
| 20 | Microsoft.SqlServer.Management.SqlParser | 1341 | 0 | 8.5 |
| 21 | Microsoft.VisualStudio.Data.Tools.Package | 1562 | 29 | 8.2 |
| 22 | OpenAI | 2217 | 0 | 7.6 |
| 23 | Microsoft.SqlServer.Smo | 792 | 0 | 6.4 |
| 24 | Roslyn.Compilers.CSharp | 988 | 0 | 6.4 |
| 25 | Roslyn.Compilers.CSharp.USql | 988 | 0 | 6.4 |

## fatia shell

| # | assembly | .cs | .xaml | MB |
|---|---|---|---|---|
| 1 | Microsoft.mshtml | 3611 | 0 | 21.3 |
| 2 | phx | 1815 | 0 | 18.1 |
| 3 | Microsoft.VisualStudio.Shell.UI.Internal | 3047 | 286 | 14.7 |
| 4 | EntityFramework | 1670 | 0 | 7.4 |
| 5 | Microsoft.VisualStudio.DesignTools.SurfaceDesigner | 2142 | 0 | 7.4 |
| 6 | System.Design | 916 | 0 | 6.3 |
| 7 | Microsoft.VisualStudio.Debugger.Engine | 2337 | 0 | 5.3 |
| 8 | Microsoft.VisualStudio.CommonIDE | 915 | 12 | 5.1 |
| 9 | Microsoft.Diagnostics.Tracing.TraceEvent | 1638 | 0 | 5.0 |
| 10 | Microsoft.VisualStudio.Interop | 3635 | 0 | 4.8 |
| 11 | Microsoft.VSDesigner | 811 | 0 | 4.0 |
| 12 | Microsoft.VisualStudio.Shell.15.0 | 919 | 37 | 3.6 |
| 13 | Microsoft.IntelliTrace.Core | 871 | 0 | 3.6 |
| 14 | Microsoft.VisualStudio.RpcContracts | 357 | 0 | 3.5 |
| 15 | Microsoft.VisualStudio.VC | 751 | 20 | 3.4 |
| 16 | Microsoft.VisualStudio.Shell.Connected | 1073 | 65 | 3.1 |
| 17 | Microsoft.VisualStudio.Platform.WindowManagement | 456 | 60 | 3.0 |
| 18 | Microsoft.VisualStudio.Modeling.Sdk.Diagrams.GraphObject | 267 | 0 | 2.8 |
| 19 | Microsoft.VisualStudio.DesignTools.XamlLanguageServiceBase | 613 | 0 | 2.5 |
| 20 | Microsoft.Data.OData | 591 | 0 | 2.3 |
| 21 | Azure.ResourceManager | 304 | 0 | 2.2 |
| 22 | System.Reactive | 381 | 0 | 2.2 |
| 23 | Architecture-msil | 172 | 0 | 2.1 |
| 24 | Microsoft.VisualStudio.Setup | 884 | 0 | 2.1 |
| 25 | Microsoft.Msagl | 413 | 0 | 1.9 |

## fatia theme

| # | assembly | .cs | .xaml | MB |
|---|---|---|---|---|
| 1 | decompiled | 3047 | 286 | 14.7 |

## fatia winforms

| # | assembly | .cs | .xaml | MB |
|---|---|---|---|---|
| 1 | Microsoft.VisualStudio.Data.Tools.Package | 1562 | 29 | 8.2 |
| 2 | System.Design | 916 | 0 | 6.3 |
| 3 | VsDebugPresentationPackage | 1187 | 71 | 5.9 |
| 4 | Microsoft.VisualStudio.Platform.VSEditor | 1585 | 10 | 5.5 |
| 5 | Microsoft.VisualStudio.Editor.Implementation | 1148 | 31 | 5.3 |
| 6 | Microsoft.VisualStudio.CommonIDE | 915 | 12 | 5.1 |
| 7 | Microsoft.VisualStudio.ProjectSystem.VS.Implementation | 758 | 18 | 4.1 |
| 8 | Microsoft.VSDesigner | 811 | 0 | 4.0 |
| 9 | VsGraphicsStandalonePkg | 804 | 49 | 3.5 |
| 10 | Microsoft.PythonTools | 648 | 18 | 2.8 |
| 11 | Microsoft.VisualStudio.Project.VisualC.VCProjectEngine | 388 | 1 | 2.8 |
| 12 | Microsoft.TeamFoundation.Git.Provider | 476 | 34 | 2.6 |
| 13 | Extensions__Microsoft__Copilot__Microsoft.TeamFoundation.Client | 871 | 0 | 2.5 |
| 14 | Microsoft.VisualStudio.ApplicationInsights | 618 | 52 | 2.5 |
| 15 | TestTools__TeamExplorerClient__Microsoft.TeamFoundation.Client | 871 | 0 | 2.4 |
| 16 | Microsoft.TeamFoundation.Client | 871 | 0 | 2.4 |
| 17 | Sqlmgmt | 589 | 0 | 2.4 |
| 18 | VsGraphicsDebuggerPkg | 609 | 12 | 2.4 |
| 19 | Microsoft.VisualStudio.TeamFoundation.VersionControl | 389 | 13 | 2.2 |
| 20 | Microsoft.VisualStudio.Editors | 245 | 1 | 1.9 |
| 21 | Microsoft.VisualStudio.ConnectedServices.Package2 | 669 | 43 | 1.9 |
| 22 | Microsoft.Data.Tools.Schema.Utilities.Sql | 241 | 0 | 1.8 |
| 23 | Microsoft.XmlEditor | 317 | 0 | 1.8 |
| 24 | Microsoft.VisualStudio.Web | 542 | 2 | 1.8 |
| 25 | Microsoft.Publish.Framework | 641 | 40 | 1.7 |
