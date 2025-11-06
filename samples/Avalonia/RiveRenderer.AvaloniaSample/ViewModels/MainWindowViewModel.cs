using System;
using CommunityToolkit.Mvvm.ComponentModel;
using RiveRenderer;

namespace RiveRenderer.AvaloniaSample.ViewModels;

public partial class MainWindowViewModel : ViewModelBase
{
    [ObservableProperty]
    private string statusMessage = "Initialising renderer...";

    public MainWindowViewModel() => StatusMessage = "Waiting for renderer initialisation…";
}
