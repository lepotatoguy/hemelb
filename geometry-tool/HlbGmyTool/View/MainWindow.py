# This file is part of HemeLB and is Copyright (C)
# the HemeLB team and/or their institutions, as detailed in the
# file AUTHORS. This software is provided under the terms of the
# license in the file LICENSE.

import wx

from .ToolPanel import ToolPanel
from .VtkViewPanel import VtkViewPanel


class MainWindow(wx.Frame):
    def __init__(self, controller):
        wx.Frame.__init__(self, None, title="HemeLB Setup Tool")
        self.appController = controller

        # self._InitMenu()
        # self._InitStatusBar() # A Statusbar in the bottom of the window

        # Going to have a vertically split window; tools on the left,
        # render window on the right.
        self.splitter = wx.SplitterWindow(self)

        self.toolPanel = ToolPanel(controller, self.splitter)
        self.vtkPanel = VtkViewPanel(controller.Pipeline, self.splitter)
        tools = self.toolPanel.GetSizer().GetMinSize()
        tools.width = max(tools.width + self.FromDIP(20), self.FromDIP(480))
        available = wx.Display().GetClientArea().GetSize()
        width = min(tools.width + self.FromDIP(420), available.width)
        height = min(max(tools.height, self.FromDIP(720)), available.height)
        self.SetClientSize((width, height))
        self.SetMinSize(self.FromDIP((640, 360)))
        self.splitter.SetMinimumPaneSize(self.FromDIP(160))
        self.splitter.SetSashGravity(0.0)
        self.splitter.SplitVertically(
            self.toolPanel, self.vtkPanel, min(tools.width, width - self.FromDIP(320))
        )

        layout = wx.BoxSizer(wx.VERTICAL)
        layout.Add(self.splitter, 1, wx.EXPAND)
        self.SetSizer(layout)
        self.Layout()

        self.Show(True)

        return

    def _InitMenu(self):
        """Do stuff for the menu."""
        filemenu = wx.Menu()

        # wx.ID_ABOUT and wx.ID_EXIT are standard IDs provided by wxWidgets.
        filemenu.Append(wx.ID_ABOUT, "&About", " Information about this program")
        filemenu.AppendSeparator()
        filemenu.Append(wx.ID_EXIT, "E&xit", " Terminate the program")

        # Creating the menubar.
        menuBar = wx.MenuBar()
        menuBar.Append(filemenu, "&File")  # Adding the "filemenu" to the MenuBar
        self.SetMenuBar(menuBar)  # Adding the MenuBar to the Frame content.

        return

    pass
