/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

using System;
using System.Text;
using System.IO;
using System.Collections.Generic;
using UnrealBuildTool;

public class ue_raytracing_lidar_featureRuntime : ModuleRules
{
    public ue_raytracing_lidar_featureRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GEMSTAR_Interfaces" });
        PrivateDependencyModuleNames.AddRange(
            new[] { "Projects", "RHI", "RenderCore", "Renderer", "GameFeatures", "ModularGameplay" });
        if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion == 7)
        {
            PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("Renderer"), "Private"));
            PrivateIncludePaths.Add(System.IO.Path.Combine(GetModuleDirectory("Renderer"), "Internal"));
        }

        string DestinationDir =
        Path.Combine(
            Target.ProjectFile.Directory.FullName,
            "Simulation"
        );

        if (!Directory.Exists(DestinationDir))
        {
            Directory.CreateDirectory(DestinationDir);
        }

        string SourceIni = Path.Combine(
            PluginDirectory,
            "Resources",
            "GemstarFeature.ini"
        );

        string TargetIni = Path.Combine(
            Target.ProjectFile.Directory.FullName,
            "Simulation",
            "DefaultGemstar.ini"
        );
        MergeMissingIniSections(
            SourceIni,
            TargetIni
        );

    }

    private static void MergeMissingIniSections(
    string SourceIni,
    string TargetIni)
    {
        if (!File.Exists(SourceIni))
        {
            return;
        }

        string TargetDir = Path.GetDirectoryName(TargetIni);

        if (!Directory.Exists(TargetDir))
        {
            Directory.CreateDirectory(TargetDir);
        }

        if (!File.Exists(TargetIni))
        {
            File.WriteAllText(TargetIni, "", Encoding.UTF8);
        }

        string SourceText = File.ReadAllText(SourceIni, Encoding.UTF8);
        string TargetText = File.ReadAllText(TargetIni, Encoding.UTF8);

        Dictionary<string, string> SourceSections =
            ParseIniSections(SourceText);

        HashSet<string> ExistingTargetSections =
            ParseIniSectionNames(TargetText);

        StringBuilder Output = new StringBuilder(TargetText);

        bool Changed = false;

        foreach (KeyValuePair<string, string> Section in SourceSections)
        {
            string SectionName = Section.Key;

            if (ExistingTargetSections.Contains(SectionName))
            {
                continue;
            }

            if (Output.Length > 0 && !Output.ToString().EndsWith("\n"))
            {
                Output.AppendLine();
            }

            Output.AppendLine();
            Output.Append(Section.Value.TrimEnd());
            Output.AppendLine();

            Changed = true;
        }

        if (Changed)
        {
            File.WriteAllText(
                TargetIni,
                Output.ToString(),
                Encoding.UTF8
            );
        }
    }

    private static Dictionary<string, string> ParseIniSections(
    string IniText)
    {
        Dictionary<string, string> Sections =
            new Dictionary<string, string>();

        using StringReader Reader = new StringReader(IniText);

        string CurrentSectionName = null;
        StringBuilder CurrentSectionText = null;
        string Line;

        while ((Line = Reader.ReadLine()) != null)
        {
            string Trimmed = Line.Trim();

            bool IsNormalSection = Trimmed.StartsWith("[");
            bool IsCommentedSection = Trimmed.StartsWith(";[");

            if ((IsNormalSection || IsCommentedSection)
                && Trimmed.EndsWith("]"))
            {
                if (CurrentSectionName != null &&
                    CurrentSectionText != null)
                {
                    Sections[CurrentSectionName] =
                        CurrentSectionText.ToString();
                }

                int SectionNameStart = IsCommentedSection ? 2 : 1;

                CurrentSectionName = Trimmed.Substring(
                    SectionNameStart,
                    Trimmed.Length - SectionNameStart - 1
                );

                CurrentSectionText = new StringBuilder();
                CurrentSectionText.AppendLine(Line);
            }
            else if (CurrentSectionText != null)
            {
                CurrentSectionText.AppendLine(Line);
            }
        }

        if (CurrentSectionName != null &&
            CurrentSectionText != null)
        {
            Sections[CurrentSectionName] =
                CurrentSectionText.ToString();
        }

        return Sections;
    }

    private static HashSet<string> ParseIniSectionNames(
    string IniText)
    {
        HashSet<string> SectionNames =
            new HashSet<string>();

        using StringReader Reader = new StringReader(IniText);

        string Line;

        while ((Line = Reader.ReadLine()) != null)
        {
            string Trimmed = Line.Trim();

            bool IsNormalSection = Trimmed.StartsWith("[");
            bool IsCommentedSection = Trimmed.StartsWith(";[");

            if ((IsNormalSection || IsCommentedSection)
                && Trimmed.EndsWith("]"))
            {
                int SectionNameStart = IsCommentedSection ? 2 : 1;

                string SectionName = Trimmed.Substring(
                    SectionNameStart,
                    Trimmed.Length - SectionNameStart - 1
                );

                SectionNames.Add(SectionName);
            }
        }

        return SectionNames;
    }

}
