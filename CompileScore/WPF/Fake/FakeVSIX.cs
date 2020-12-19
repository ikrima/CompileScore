﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace CompileScore
{
    public class MacroEvaluator
    {
        public string Evaluate(string input) { return input; }
    }

    public class SolutionSettings
    {
        public string ScoreLocation { set; get; } = "";
    }

    public class SettingsManager
    {
        public static event Notify SettingsChanged;

        private static readonly Lazy<SettingsManager> lazy = new Lazy<SettingsManager>(() => new SettingsManager());
        public static SettingsManager Instance { get { return lazy.Value; } }

        public SolutionSettings Settings { set; get; } = new SolutionSettings();

        public void Initialize(string solutionDir) { }

        public void DummyFunction() { SettingsChanged?.Invoke(); }
    }

    public delegate void NotifySolution(Solution solution);

    public class SolutionEventsListener
    {
        private static readonly Lazy<SolutionEventsListener> lazy = new Lazy<SolutionEventsListener>(() => new SolutionEventsListener());
        public static SolutionEventsListener Instance { get { return lazy.Value; } }

        public event NotifySolution SolutionReady;
        public event Notify ActiveSolutionConfigurationChanged;

        public void DummyFunction() { SolutionReady?.Invoke(new Solution()); ActiveSolutionConfigurationChanged?.Invoke(); }

    }
}