/***************************************************************************
 *   Copyright (C) 2008-2009 by Andrzej Rybczak                            *
 *   electricityispower@gmail.com                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <cerrno>
#include <clocale>
#include <csignal>
#include <cstring>

#include <iostream>
#include <fstream>
#include <stdexcept>

#include "mpdpp.h"
#include "ncmpcpp.h"

#include "browser.h"
#include "charset.h"
#include "clock.h"
#include "display.h"
#include "global.h"
#include "help.h"
#include "helpers.h"
#include "media_library.h"
#include "lyrics.h"
#include "playlist.h"
#include "playlist_editor.h"
#include "search_engine.h"
#include "settings.h"
#include "song.h"
#include "info.h"
#include "outputs.h"
#include "status.h"
#include "tag_editor.h"

#define CHECK_PLAYLIST_FOR_FILTERING									\
			if (myPlaylist->Main()->isFiltered())						\
			{										\
				ShowMessage("%s", MPD::Message::FunctionDisabledFilteringEnabled);	\
				continue;								\
			}

#define CHECK_MPD_MUSIC_DIR										\
			if (Config.mpd_music_dir.empty())						\
			{										\
				ShowMessage("configuration variable mpd_music_dir is not set!");	\
				continue;								\
			}

using namespace Global;
using namespace MPD;

BasicScreen *Global::myScreen;
BasicScreen *Global::myOldScreen;

Window *Global::wHeader;
Window *Global::wFooter;

size_t Global::MainStartY;
size_t Global::MainHeight;

bool Global::BlockItemListUpdate = 0;

bool Global::MessagesAllowed = 0;
bool Global::RedrawHeader = 1;

int main(int argc, char *argv[])
{
	CreateConfigDir();
	DefaultConfiguration(Config);
	DefaultKeys(Key);
	ReadConfiguration(Config);
	ReadKeys(Key);
	
	if (getenv("MPD_HOST"))
		Mpd.SetHostname(getenv("MPD_HOST"));
	if (getenv("MPD_PORT"))
		Mpd.SetPort(atoi(getenv("MPD_PORT")));
	
	if (Config.mpd_host != "localhost")
		Mpd.SetHostname(Config.mpd_host);
	if (Config.mpd_port != 6600)
		Mpd.SetPort(Config.mpd_port);
	
	Mpd.SetTimeout(Config.mpd_connection_timeout);
	
	if (argc > 1)
	{
		ParseArgv(argc, argv);
	}
	
	if (!ConnectToMPD())
		return -1;
	
	// redirect std::cerr output to ~/.ncmpcpp/error.log file
	std::ofstream errorlog((config_dir + "error.log").c_str(), std::ios::app);
	std::streambuf *cerr_buffer = std::cerr.rdbuf();
	std::cerr.rdbuf(errorlog.rdbuf());
	
	InitScreen("ncmpc++ ver. "VERSION, Config.colors_enabled);
	
	bool real_header_visibility = Config.header_visibility;
	bool real_statusbar_visibility = Config.statusbar_visibility;
	
	if (Config.new_design)
	{
		Config.header_visibility = 1;
		Config.statusbar_visibility = 0;
	}
	
	size_t header_height, footer_start_y, footer_height;
	SetWindowsDimensions(header_height, footer_start_y, footer_height);
	
	if (Config.header_visibility)
	{
		wHeader = new Window(0, 0, COLS, header_height, "", Config.header_color, brNone);
		wHeader->Display();
	}
	
	wFooter = new Window(0, footer_start_y, COLS, footer_height, "", Config.statusbar_color, brNone);
	wFooter->SetTimeout(ncmpcpp_window_timeout);
	wFooter->SetGetStringHelper(StatusbarGetStringHelper);
	wFooter->CreateHistory();
	
	myPlaylist->SwitchTo();
	myPlaylist->UpdateTimer();
	
	Mpd.SetStatusUpdater(NcmpcppStatusChanged, NULL);
	Mpd.SetErrorHandler(NcmpcppErrorCallback, NULL);
	
	// local variables
	int input;
	
	bool main_exit = 0;
	bool design_changed = 0;
	bool title_allowed = !Config.display_screens_numbers_on_start;
	
	std::string screen_title;
	
	timeval now, past;
	// local variables end
	
#	ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#	endif // !WIN32
	
	gettimeofday(&now, 0);
	
	MEVENT mouse_event;
	mouseinterval(0);
	if (Config.mouse_support)
		mousemask(ALL_MOUSE_EVENTS, 0);
	
	while (!main_exit)
	{
		if (!Mpd.Connected())
		{
			ShowMessage("Attempting to reconnect...");
			if (Mpd.Connect())
			{
				ShowMessage("Connected to %s!", Mpd.GetHostname().c_str());
				MessagesAllowed = 0;
				UpdateStatusImmediately = 1;
#				ifdef ENABLE_OUTPUTS
				myOutputs->FetchList();
#				endif // ENABLE_OUTPUTS
			}
		}
		
		TraceMpdStatus();
		
		MessagesAllowed = 1;
		
		// header stuff
		gettimeofday(&past, 0);
		if (((past.tv_sec == now.tv_sec && past.tv_usec >= now.tv_usec+500000) || past.tv_sec > now.tv_sec)
		&&   (myScreen == myPlaylist || myScreen == myBrowser || myScreen == myLyrics)
		   )
		{
			RedrawHeader = 1;
			gettimeofday(&now, 0);
		}
		if (Config.header_visibility && RedrawHeader)
		{
			if (title_allowed)
			{
				if (Config.new_design)
				{
					std::basic_string<my_char_t> title = myScreen->Title();
					*wHeader << XY(0, 3) << wclrtoeol;
					*wHeader << fmtBold << clBlack;
					mvwhline(wHeader->Raw(), 2, 0, 0, COLS);
					mvwhline(wHeader->Raw(), 4, 0, 0, COLS);
					*wHeader << XY((COLS-Window::Length(title))/2, 3);
					*wHeader << Config.statusbar_color << title << clEnd;
					*wHeader << clEnd << fmtBoldEnd;
				}
				else
					*wHeader << XY(0, 0) << wclrtoeol << fmtBold << myScreen->Title() << fmtBoldEnd;
			}
			else
			{
				*wHeader << XY(0, Config.new_design ? 3 : 0)
				<< fmtBold << char(Key.Help[0]) << fmtBoldEnd << ":Help  "
				<< fmtBold << char(Key.Playlist[0]) << fmtBoldEnd << ":Playlist  "
				<< fmtBold << char(Key.Browser[0]) << fmtBoldEnd << ":Browse  "
				<< fmtBold << char(Key.SearchEngine[0]) << fmtBoldEnd << ":Search  "
				<< fmtBold << char(Key.MediaLibrary[0]) << fmtBoldEnd << ":Library  "
				<< fmtBold << char(Key.PlaylistEditor[0]) << fmtBoldEnd << ":Playlist editor";
#				ifdef HAVE_TAGLIB_H
				*wHeader << "  " << fmtBold << char(Key.TagEditor[0]) << fmtBoldEnd << ":Tag editor";
#				endif // HAVE_TAGLIB_H
#				ifdef ENABLE_CLOCK
				*wHeader << "  " << fmtBold << char(Key.Clock[0]) << fmtBoldEnd << ":Clock";
#				endif // ENABLE_CLOCK
				if (Config.new_design)
				{
					*wHeader << fmtBold << clBlack;
					mvwhline(wHeader->Raw(), 2, 0, 0, COLS);
					mvwhline(wHeader->Raw(), 4, 0, 0, COLS);
					*wHeader << clEnd << fmtBoldEnd;
				}
			}
			
			wHeader->SetColor(Config.volume_color);
			*wHeader << XY(wHeader->GetWidth()-VolumeState.length(), 0) << VolumeState;
			wHeader->SetColor(Config.header_color);
			wHeader->Refresh();
			RedrawHeader = 0;
		}
		// header stuff end
		
		myScreen->Update();
		myScreen->RefreshWindow();
		myScreen->ReadKey(input);
		
		if (input == ERR)
			continue;
		
		if (!title_allowed)
			RedrawHeader = 1;
		title_allowed = 1;
		
		if (myScreen == myPlaylist)
			myPlaylist->EnableHighlighting();
		else if (
		   myScreen == myLibrary
		|| myScreen == myPlaylistEditor
#		ifdef HAVE_TAGLIB_H
		|| myScreen == myTagEditor
#		endif // HAVE_TAGLIB_H
			)
		{
			if (Keypressed(input, Key.Up)
			||  Keypressed(input, Key.Down)
			||  Keypressed(input, Key.PageUp)
			||  Keypressed(input, Key.PageDown)
			||  Keypressed(input, Key.Home)
			||  Keypressed(input, Key.End)
			||  Keypressed(input, Key.ApplyFilter)
			||  Keypressed(input, Key.FindForward)
			||  Keypressed(input, Key.FindBackward)
			||  Keypressed(input, Key.NextFoundPosition)
			||  Keypressed(input, Key.PrevFoundPosition))
			{
				if (myScreen->ActiveWindow() == myLibrary->Artists)
				{
					myLibrary->Albums->Clear(0);
					myLibrary->Songs->Clear(0);
				}
				else if (myScreen->ActiveWindow() == myLibrary->Albums)
				{
					myLibrary->Songs->Clear(0);
				}
				else if (myScreen->ActiveWindow() == myPlaylistEditor->Playlists)
				{
					myPlaylistEditor->Content->Clear(0);
				}
#				ifdef HAVE_TAGLIB_H
				else if (myScreen->ActiveWindow() == myTagEditor->LeftColumn)
				{
					myTagEditor->Tags->Clear(0);
					myTagEditor->TagTypes->Refresh();
				}
#				endif // HAVE_TAGLIB_H
			}
		}
		
		// key mapping beginning
		
		if (Keypressed(input, Key.ToggleInterface))
		{
			Config.new_design = !Config.new_design;
			if (Config.new_design)
			{
				Config.header_visibility = 1;
				Config.statusbar_visibility = 0;
			}
			else
			{
				Config.header_visibility = real_header_visibility;
				Config.statusbar_visibility = real_statusbar_visibility;
			}
			SetWindowsDimensions(header_height, footer_start_y, footer_height);
			UnlockProgressbar();
			UnlockStatusbar();
			design_changed = 1;
		}
		if (Keypressed(input, Key.Up))
		{
			myScreen->Scroll(wUp, Key.Up);
		}
		else if (Keypressed(input, Key.Down))
		{
			myScreen->Scroll(wDown, Key.Down);
		}
		else if (Keypressed(input, Key.PageUp))
		{
			myScreen->Scroll(wPageUp, Key.PageUp);
		}
		else if (Keypressed(input, Key.PageDown))
		{
			myScreen->Scroll(wPageDown, Key.PageDown);
		}
		else if (Keypressed(input, Key.Home))
		{
			myScreen->Scroll(wHome);
		}
		else if (Keypressed(input, Key.End))
		{
			myScreen->Scroll(wEnd);
		}
		else if (Config.mouse_support && input == KEY_MOUSE)
		{
#			ifdef USE_PDCURSES
			nc_getmouse(&mouse_event);
#			else
			getmouse(&mouse_event);
#			endif // USE_PDCURSES
			if (mouse_event.bstate & BUTTON1_PRESSED
			&&  mouse_event.y == LINES-(Config.statusbar_visibility ? 2 : 1)
			   ) // progressbar
			{
				const Song *s = myPlaylist->NowPlayingSong();
				if (!s)
					continue;
				Mpd.Seek(s->GetTotalLength()*mouse_event.x/double(COLS));
				UpdateStatusImmediately = 1;
			}
			else if (mouse_event.bstate & BUTTON1_PRESSED
			     &&	 (Config.statusbar_visibility || Config.new_design)
			     &&	 Mpd.GetState() > psStop
			     &&	 mouse_event.y == (Config.new_design ? 1 : LINES-1) && mouse_event.x < 9
				) // playing/paused
			{
				Mpd.Pause();
				UpdateStatusImmediately = 1;
			}
			else if ((mouse_event.bstate & BUTTON2_PRESSED || mouse_event.bstate & BUTTON4_PRESSED)
			     &&	 Config.header_visibility
			     &&	 mouse_event.y == 0 && size_t(mouse_event.x) > COLS-VolumeState.length()
				) // volume
			{
				if (mouse_event.bstate & BUTTON2_PRESSED)
					Mpd.SetVolume(Mpd.GetVolume()-2);
				else
					Mpd.SetVolume(Mpd.GetVolume()+2);
			}
			else
				myScreen->MouseButtonPressed(mouse_event);
		}
		else if (input == KEY_RESIZE || design_changed)
		{
#			ifdef USE_PDCURSES
			resize_term(0, 0);
#			endif // USE_PDCURSES
			
			RedrawHeader = 1;
			
			if (COLS < 20 || LINES < 5)
			{
				endwin();
				std::cout << "Screen too small!\n";
				return 1;
			}
			
			MainHeight = LINES-(Config.new_design ? 7 : 4);
			
			if (!Config.header_visibility)
				MainHeight += 2;
			if (!Config.statusbar_visibility)
				MainHeight++;
			
			myHelp->hasToBeResized = 1;
			myPlaylist->hasToBeResized = 1;
			myBrowser->hasToBeResized = 1;
			mySearcher->hasToBeResized = 1;
			myLibrary->hasToBeResized = 1;
			myPlaylistEditor->hasToBeResized = 1;
			myInfo->hasToBeResized = 1;
			myLyrics->hasToBeResized = 1;
			
#			ifdef HAVE_TAGLIB_H
			myTinyTagEditor->hasToBeResized = 1;
			myTagEditor->hasToBeResized = 1;
#			endif // HAVE_TAGLIB_H
			
#			ifdef ENABLE_CLOCK
			myClock->hasToBeResized = 1;
#			endif // ENABLE_CLOCK
			
			myScreen->Resize();
			
			if (Config.header_visibility)
				wHeader->Resize(COLS, header_height);
			
			footer_start_y = LINES-(Config.statusbar_visibility ? 2 : 1);
			wFooter->MoveTo(0, footer_start_y);
			wFooter->Resize(COLS, Config.statusbar_visibility ? 2 : 1);
			
			myScreen->Refresh();
			RedrawStatusbar = 1;
			StatusChanges changes;
			if (Mpd.GetState() < psPlay || design_changed)
			{
				changes.PlayerState = 1;
				if (design_changed)
					changes.Volume = 1;
			}
			changes.StatusFlags = 1; // force status update
			NcmpcppStatusChanged(&Mpd, changes, 0);
			if (design_changed)
			{
				RedrawStatusbar = 1;
				NcmpcppStatusChanged(&Mpd, StatusChanges(), 0);
				design_changed = 0;
				ShowMessage("User interface: %s", Config.new_design ? "Alternative" : "Classic");
			}
		}
		else if (Keypressed(input, Key.GoToParentDir))
		{
			if (myScreen == myBrowser && myBrowser->CurrentDir() != "/")
			{
				myBrowser->Main()->Reset();
				myBrowser->EnterPressed();
			}
		}
		else if (Keypressed(input, Key.Enter))
		{
			myScreen->EnterPressed();
		}
		else if (Keypressed(input, Key.Space))
		{
			myScreen->SpacePressed();
		}
		else if (Keypressed(input, Key.VolumeUp))
		{
			if (myScreen == myLibrary && input == Key.VolumeUp[0])
			{
				myLibrary->NextColumn();
			}
			else if (myScreen == myPlaylistEditor && input == Key.VolumeUp[0])
			{
				myPlaylistEditor->NextColumn();
			}
#			ifdef HAVE_TAGLIB_H
			else if (myScreen == myTagEditor && input == Key.VolumeUp[0])
			{
				myTagEditor->NextColumn();
			}
#			endif // HAVE_TAGLIB_H
			else
				Mpd.SetVolume(Mpd.GetVolume()+1);
		}
		else if (Keypressed(input, Key.VolumeDown))
		{
			if (myScreen == myLibrary && input == Key.VolumeDown[0])
			{
				myLibrary->PrevColumn();
			}
			else if (myScreen == myPlaylistEditor && input == Key.VolumeDown[0])
			{
				myPlaylistEditor->PrevColumn();
			}
#			ifdef HAVE_TAGLIB_H
			else if (myScreen == myTagEditor && input == Key.VolumeDown[0])
			{
				myTagEditor->PrevColumn();
			}
#			endif // HAVE_TAGLIB_H
			else
				Mpd.SetVolume(Mpd.GetVolume()-1);
		}
		else if (Keypressed(input, Key.Delete))
		{
			if (!myPlaylist->Main()->Empty() && myScreen == myPlaylist)
			{
				Playlist::BlockUpdate = 1;
				if (myPlaylist->Main()->hasSelected())
				{
					std::vector<size_t> list;
					myPlaylist->Main()->GetSelected(list);
					Mpd.StartCommandsList();
					for (std::vector<size_t>::reverse_iterator it = list.rbegin(); it != list.rend(); ++it)
					{
						Mpd.DeleteID((*myPlaylist->Main())[*it].GetID());
						myPlaylist->Main()->DeleteOption(*it);
					}
					Mpd.CommitCommandsList();
					myPlaylist->FixPositions(list.front());
					ShowMessage("Selected items deleted!");
				}
				else
				{
					Playlist::BlockNowPlayingUpdate = 1;
					myPlaylist->Main()->SetTimeout(50);
					int del_counter = 0;
					while (!myPlaylist->Main()->Empty() && Keypressed(input, Key.Delete))
					{
						size_t id = myPlaylist->Main()->Choice();
						TraceMpdStatus();
						Playlist::BlockUpdate = 1;
						myPlaylist->UpdateTimer();
						// needed for keeping proper position of now playing song.
						if (myPlaylist->NowPlaying > myPlaylist->CurrentSong()->GetPosition()-del_counter)
							myPlaylist->NowPlaying--;
						Mpd.DeleteID(myPlaylist->CurrentSong()->GetID());
						myPlaylist->Main()->DeleteOption(id);
						myPlaylist->Main()->Refresh();
						myPlaylist->Main()->ReadKey(input);
						del_counter++;
					}
					myPlaylist->FixPositions(myPlaylist->Main()->Choice());
					myPlaylist->Main()->SetTimeout(ncmpcpp_window_timeout);
					Playlist::BlockNowPlayingUpdate = 0;
				}
			}
			else if (
				 (myScreen == myBrowser && !myBrowser->Main()->Empty() && myBrowser->Main()->Current().type == itPlaylist)
			||       (myScreen->ActiveWindow() == myPlaylistEditor->Playlists)
				)
			{
				LockStatusbar();
				std::string name = myScreen == myBrowser ? myBrowser->Main()->Current().name : myPlaylistEditor->Playlists->Current();
				Statusbar() << "Delete playlist \"" << name << "\" ? [y/n] ";
				curs_set(1);
				int in = 0;
				do
				{
					TraceMpdStatus();
					wFooter->ReadKey(in);
				}
				while (in != 'y' && in != 'n');
				if (in == 'y')
				{
					Mpd.DeletePlaylist(locale_to_utf_cpy(name));
					ShowMessage("Playlist \"%s\" deleted!", name.c_str());
					if (!Config.local_browser && myBrowser->Main())
						myBrowser->GetDirectory("/");
				}
				else
					ShowMessage("Aborted!");
				curs_set(0);
				if (myPlaylistEditor->Main()) // check if initialized
					myPlaylistEditor->Playlists->Clear(0); // make playlists list update itself
				UnlockStatusbar();
			}
			else if (myScreen == myBrowser && !myBrowser->Main()->Empty() && myBrowser->Main()->Current().type != itPlaylist)
			{
				if (!Config.local_browser)
					CHECK_MPD_MUSIC_DIR;
				
				MPD::Item &item = myBrowser->Main()->Current();
				
				if (item.type == itSong && !Config.allow_physical_files_deletion)
				{
					ShowMessage("Deleting files is disabled by default, see man page for more details");
					continue;
				}
				if (item.type == itDirectory && !Config.allow_physical_directories_deletion)
				{
					ShowMessage("Deleting directories is disabled by default, see man page for more details");
					continue;
				}
				if (item.type == itDirectory && item.song) // parent dir
					continue;
				
				LockStatusbar();
				std::string name = item.type == itSong ? item.song->GetName() : item.name;
				Statusbar() << "Delete " << (item.type == itSong ? "file" : "directory") << " \"" << name << "\" ? [y/n] ";
				curs_set(1);
				int in = 0;
				do
				{
					TraceMpdStatus();
					wFooter->ReadKey(in);
				}
				while (in != 'y' && in != 'n');
				if (in == 'y')
				{
					std::string path;
					if (!Config.local_browser)
						path = Config.mpd_music_dir;
					path += item.type == itSong ? item.song->GetFile() : item.name;
					
					if (item.type == itDirectory)
						myBrowser->ClearDirectory(path);
					
					if (remove(path.c_str()) == 0)
					{
						ShowMessage("\"%s\" has been successfuly deleted!", name.c_str());
						if (!Config.local_browser)
							Mpd.UpdateDirectory(myBrowser->CurrentDir());
						else
							myBrowser->GetDirectory(myBrowser->CurrentDir());
					}
					else
						ShowMessage("Couldn't remove \"%s\": %s", name.c_str(), strerror(errno));
				}
				else
					ShowMessage("Aborted!");
				curs_set(0);
				UnlockStatusbar();
			}
			else if (myScreen->ActiveWindow() == myPlaylistEditor->Content && !myPlaylistEditor->Content->Empty())
			{
				if (myPlaylistEditor->Content->hasSelected())
				{
					std::vector<size_t> list;
					myPlaylistEditor->Content->GetSelected(list);
					std::string playlist = locale_to_utf_cpy(myPlaylistEditor->Playlists->Current());
					ShowMessage("Deleting selected items...");
					Mpd.StartCommandsList();
					for (std::vector<size_t>::reverse_iterator it = list.rbegin(); it != list.rend(); ++it)
					{
						Mpd.Delete(playlist, *it);
						myPlaylistEditor->Content->DeleteOption(*it);
					}
					Mpd.CommitCommandsList();
					ShowMessage("Selected items deleted from playlist \"%s\"!", myPlaylistEditor->Playlists->Current().c_str());
				}
				else
				{
					myPlaylistEditor->Content->SetTimeout(50);
					locale_to_utf(myPlaylistEditor->Playlists->Current());
					while (!myPlaylistEditor->Content->Empty() && Keypressed(input, Key.Delete))
					{
						TraceMpdStatus();
						myPlaylist->UpdateTimer();
						Mpd.Delete(myPlaylistEditor->Playlists->Current(), myPlaylistEditor->Content->Choice());
						myPlaylistEditor->Content->DeleteOption(myPlaylistEditor->Content->Choice());
						myPlaylistEditor->Content->Refresh();
						myPlaylistEditor->Content->ReadKey(input);
					}
					utf_to_locale(myPlaylistEditor->Playlists->Current());
					myPlaylistEditor->Content->SetTimeout(ncmpcpp_window_timeout);
				}
			}
		}
		else if (Keypressed(input, Key.Prev))
		{
			Mpd.Prev();
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.Next))
		{
			Mpd.Next();
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.Pause))
		{
			Mpd.Pause();
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.SavePlaylist))
		{
			LockStatusbar();
			Statusbar() << "Save playlist as: ";
			std::string playlist_name = wFooter->GetString();
			std::string real_playlist_name = locale_to_utf_cpy(playlist_name);
			UnlockStatusbar();
			if (playlist_name.find("/") != std::string::npos)
			{
				ShowMessage("Playlist name cannot contain slashes!");
				continue;
			}
			if (!playlist_name.empty())
			{
				if (myPlaylist->Main()->isFiltered())
				{
					Mpd.StartCommandsList();
					for (size_t i = 0; i < myPlaylist->Main()->Size(); ++i)
						Mpd.AddToPlaylist(real_playlist_name, (*myPlaylist->Main())[i]);
					Mpd.CommitCommandsList();
					if (Mpd.GetErrorMessage().empty())
						ShowMessage("Filtered items added to playlist \"%s\"", playlist_name.c_str());
				}
				else if (Mpd.SavePlaylist(real_playlist_name))
				{
					ShowMessage("Playlist saved as: %s", playlist_name.c_str());
					if (myPlaylistEditor->Main()) // check if initialized
						myPlaylistEditor->Playlists->Clear(0); // make playlist's list update itself
				}
				else
				{
					LockStatusbar();
					Statusbar() << "Playlist already exists, overwrite: " << playlist_name << " ? [y/n] ";
					curs_set(1);
					int in = 0;
					MessagesAllowed = 0;
					while (in != 'y' && in != 'n')
					{
						TraceMpdStatus();
						wFooter->ReadKey(in);
					}
					MessagesAllowed = 1;
					
					if (in == 'y')
					{
						Mpd.DeletePlaylist(real_playlist_name);
						if (Mpd.SavePlaylist(real_playlist_name))
							ShowMessage("Playlist overwritten!");
					}
					else
						ShowMessage("Aborted!");
					curs_set(0);
					if (myPlaylistEditor->Main()) // check if initialized
						myPlaylistEditor->Playlists->Clear(0); // make playlist's list update itself
					UnlockStatusbar();
					if (myScreen == myPlaylist)
						myPlaylist->EnableHighlighting();
				}
			}
			if (!Config.local_browser
			&&  myBrowser->Main()
			&&  myBrowser->CurrentDir() == "/"
			&&  !myBrowser->Main()->Empty())
				myBrowser->GetDirectory(myBrowser->CurrentDir());
		}
		else if (Keypressed(input, Key.Stop))
		{
			Mpd.Stop();
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.MvSongUp))
		{
			if (myScreen == myPlaylist && !myPlaylist->Main()->Empty())
			{
				CHECK_PLAYLIST_FOR_FILTERING;
				myPlaylist->Main()->SetTimeout(50);
				if (myPlaylist->Main()->hasSelected())
				{
					std::vector<size_t> list;
					myPlaylist->Main()->GetSelected(list);
					
					for (std::vector<size_t>::iterator it = list.begin(); it != list.end(); ++it)
						if (*it == size_t(myPlaylist->NowPlaying) && list.front() > 0)
							myPlaylist->Main()->BoldOption(myPlaylist->NowPlaying, 0);
					
					std::vector<size_t> origs(list);
					
					while (Keypressed(input, Key.MvSongUp) && list.front() > 0)
					{
						TraceMpdStatus();
						Playlist::BlockUpdate = 1;
						myPlaylist->UpdateTimer();
						for (std::vector<size_t>::iterator it = list.begin(); it != list.end(); ++it)
						{
							--*it;
							myPlaylist->Main()->at((*it)+1).SetPosition(*it);
							myPlaylist->Main()->at(*it).SetPosition((*it)+1);
							myPlaylist->Main()->Swap(*it, (*it)+1);
						}
						myPlaylist->Main()->Highlight(list[(list.size()-1)/2]);
						myPlaylist->Main()->Refresh();
						myPlaylist->Main()->ReadKey(input);
					}
					Mpd.StartCommandsList();
					for (size_t i = 0; i < list.size(); ++i)
						Mpd.Move(origs[i], list[i]);
					Mpd.CommitCommandsList();
				}
				else
				{
					size_t from, to;
					from = to = myPlaylist->Main()->Choice();
					// unbold now playing as if song changes during move, this won't be unbolded.
					if (to == size_t(myPlaylist->NowPlaying) && to > 0)
						myPlaylist->Main()->BoldOption(myPlaylist->NowPlaying, 0);
					while (Keypressed(input, Key.MvSongUp) && to > 0)
					{
						TraceMpdStatus();
						Playlist::BlockUpdate = 1;
						myPlaylist->UpdateTimer();
						--to;
						myPlaylist->Main()->at(from).SetPosition(to);
						myPlaylist->Main()->at(to).SetPosition(from);
						myPlaylist->Main()->Swap(to, to+1);
						myPlaylist->Main()->Scroll(wUp);
						myPlaylist->Main()->Refresh();
						myPlaylist->Main()->ReadKey(input);
					}
					Mpd.Move(from, to);
					UpdateStatusImmediately = 1;
				}
				myPlaylist->Main()->SetTimeout(ncmpcpp_window_timeout);
			}
			else if (myScreen->ActiveWindow() == myPlaylistEditor->Content && !myPlaylistEditor->Content->Empty())
			{
				myPlaylistEditor->Content->SetTimeout(50);
				if (myPlaylistEditor->Content->hasSelected())
				{
					std::vector<size_t> list;
					myPlaylistEditor->Content->GetSelected(list);
					
					std::vector<size_t> origs(list);
					
					while (Keypressed(input, Key.MvSongUp) && list.front() > 0)
					{
						TraceMpdStatus();
						myPlaylist->UpdateTimer();
						for (std::vector<size_t>::iterator it = list.begin(); it != list.end(); ++it)
						{
							--*it;
							myPlaylistEditor->Content->Swap(*it, (*it)+1);
						}
						myPlaylistEditor->Content->Highlight(list[(list.size()-1)/2]);
						myPlaylistEditor->Content->Refresh();
						myPlaylistEditor->Content->ReadKey(input);
					}
					Mpd.StartCommandsList();
					for (size_t i = 0; i < list.size(); ++i)
						if (origs[i] != list[i])
							Mpd.Move(myPlaylistEditor->Playlists->Current(), origs[i], list[i]);
					Mpd.CommitCommandsList();
				}
				else
				{
					size_t from, to;
					from = to = myPlaylistEditor->Content->Choice();
					while (Keypressed(input, Key.MvSongUp) && to > 0)
					{
						TraceMpdStatus();
						myPlaylist->UpdateTimer();
						--to;
						myPlaylistEditor->Content->Swap(to, to+1);
						myPlaylistEditor->Content->Scroll(wUp);
						myPlaylistEditor->Content->Refresh();
						myPlaylistEditor->Content->ReadKey(input);
					}
					if (from != to)
						Mpd.Move(myPlaylistEditor->Playlists->Current(), from, to);
				}
				myPlaylistEditor->Content->SetTimeout(ncmpcpp_window_timeout);
			}
		}
		else if (Keypressed(input, Key.MvSongDown))
		{
			if (myScreen == myPlaylist && !myPlaylist->Main()->Empty())
			{
				CHECK_PLAYLIST_FOR_FILTERING;
				myPlaylist->Main()->SetTimeout(50);
				if (myPlaylist->Main()->hasSelected())
				{
					std::vector<size_t> list;
					myPlaylist->Main()->GetSelected(list);
					
					for (std::vector<size_t>::iterator it = list.begin(); it != list.end(); ++it)
						if (*it == size_t(myPlaylist->NowPlaying) && list.back() < myPlaylist->Main()->Size()-1)
							myPlaylist->Main()->BoldOption(myPlaylist->NowPlaying, 0);
					
					std::vector<size_t> origs(list);
					
					while (Keypressed(input, Key.MvSongDown) && list.back() < myPlaylist->Main()->Size()-1)
					{
						TraceMpdStatus();
						Playlist::BlockUpdate = 1;
						myPlaylist->UpdateTimer();
						for (std::vector<size_t>::reverse_iterator it = list.rbegin(); it != list.rend(); ++it)
						{
							++*it;
							myPlaylist->Main()->at((*it)-1).SetPosition(*it);
							myPlaylist->Main()->at(*it).SetPosition((*it)-1);
							myPlaylist->Main()->Swap(*it, (*it)-1);
						}
						myPlaylist->Main()->Highlight(list[(list.size()-1)/2]);
						myPlaylist->Main()->Refresh();
						myPlaylist->Main()->ReadKey(input);
					}
					Mpd.StartCommandsList();
					for (int i = list.size()-1; i >= 0; --i)
						Mpd.Move(origs[i], list[i]);
					Mpd.CommitCommandsList();
				}
				else
				{
					size_t from, to;
					from = to = myPlaylist->Main()->Choice();
					// unbold now playing as if song changes during move, this won't be unbolded.
					if (to == size_t(myPlaylist->NowPlaying) && to < myPlaylist->Main()->Size()-1)
						myPlaylist->Main()->BoldOption(myPlaylist->NowPlaying, 0);
					while (Keypressed(input, Key.MvSongDown) && to < myPlaylist->Main()->Size()-1)
					{
						TraceMpdStatus();
						Playlist::BlockUpdate = 1;
						myPlaylist->UpdateTimer();
						++to;
						myPlaylist->Main()->at(from).SetPosition(to);
						myPlaylist->Main()->at(to).SetPosition(from);
						myPlaylist->Main()->Swap(to, to-1);
						myPlaylist->Main()->Scroll(wDown);
						myPlaylist->Main()->Refresh();
						myPlaylist->Main()->ReadKey(input);
					}
					Mpd.Move(from, to);
					UpdateStatusImmediately = 1;
				}
				myPlaylist->Main()->SetTimeout(ncmpcpp_window_timeout);
				
			}
			else if (myScreen->ActiveWindow() == myPlaylistEditor->Content && !myPlaylistEditor->Content->Empty())
			{
				myPlaylistEditor->Content->SetTimeout(50);
				if (myPlaylistEditor->Content->hasSelected())
				{
					std::vector<size_t> list;
					myPlaylistEditor->Content->GetSelected(list);
					
					std::vector<size_t> origs(list);
					
					while (Keypressed(input, Key.MvSongDown) && list.back() < myPlaylistEditor->Content->Size()-1)
					{
						TraceMpdStatus();
						myPlaylist->UpdateTimer();
						for (std::vector<size_t>::reverse_iterator it = list.rbegin(); it != list.rend(); ++it)
						{
							++*it;
							myPlaylistEditor->Content->Swap(*it, (*it)-1);
						}
						myPlaylistEditor->Content->Highlight(list[(list.size()-1)/2]);
						myPlaylistEditor->Content->Refresh();
						myPlaylistEditor->Content->ReadKey(input);
					}
					Mpd.StartCommandsList();
					for (int i = list.size()-1; i >= 0; --i)
						if (origs[i] != list[i])
							Mpd.Move(myPlaylistEditor->Playlists->Current(), origs[i], list[i]);
					Mpd.CommitCommandsList();
				}
				else
				{
					size_t from, to;
					from = to = myPlaylistEditor->Content->Choice();
					while (Keypressed(input, Key.MvSongDown) && to < myPlaylistEditor->Content->Size()-1)
					{
						TraceMpdStatus();
						myPlaylist->UpdateTimer();
						++to;
						myPlaylistEditor->Content->Swap(to, to-1);
						myPlaylistEditor->Content->Scroll(wDown);
						myPlaylistEditor->Content->Refresh();
						myPlaylistEditor->Content->ReadKey(input);
					}
					if (from != to)
						Mpd.Move(myPlaylistEditor->Playlists->Current(), from, to);
				}
				myPlaylistEditor->Content->SetTimeout(ncmpcpp_window_timeout);
			}
		}
		else if (Keypressed(input, Key.MoveTo) && myScreen == myPlaylist)
		{
			CHECK_PLAYLIST_FOR_FILTERING;
			if (!myPlaylist->Main()->hasSelected())
			{
				ShowMessage("No selected items to move!");
				continue;
			}
			Playlist::BlockUpdate = 1;
			size_t pos = myPlaylist->Main()->Choice();
			 // if cursor is at the last item, break convention and move at the end of playlist
			if (pos == myPlaylist->Main()->Size()-1)
				pos++;
			std::vector<size_t> list;
			myPlaylist->Main()->GetSelected(list);
			if (pos >= list.front() && pos <= list.back())
				continue;
			int diff = pos-list.front();
			Mpd.StartCommandsList();
			if (diff > 0)
			{
				pos -= list.size();
				size_t i = list.size()-1;
				for (std::vector<size_t>::reverse_iterator it = list.rbegin(); it != list.rend(); ++it, --i)
				{
					Mpd.Move(*it, pos+i);
					myPlaylist->Main()->Move(*it, pos+i);
				}
			}
			else if (diff < 0)
			{
				size_t i = 0;
				for (std::vector<size_t>::const_iterator it = list.begin(); it != list.end(); ++it, ++i)
				{
					Mpd.Move(*it, pos+i);
					myPlaylist->Main()->Move(*it, pos+i);
				}
			}
			Mpd.CommitCommandsList();
			myPlaylist->Main()->Highlight(pos);
			myPlaylist->FixPositions();
		}
		else if (Keypressed(input, Key.Add))
		{
			if (myScreen == myPlaylistEditor && myPlaylistEditor->Playlists->Empty())
				continue;
			LockStatusbar();
			Statusbar() << (myScreen == myPlaylistEditor ? "Add to playlist: " : "Add: ");
			std::string path = wFooter->GetString();
			UnlockStatusbar();
			if (!path.empty())
			{
				SongList list;
				Mpd.GetDirectoryRecursive(path, list);
				if (!list.empty())
				{
					Mpd.StartCommandsList();
					SongList::const_iterator it = list.begin();
					if (myScreen == myPlaylistEditor)
					{
						for (; it != list.end(); ++it)
							Mpd.AddToPlaylist(myPlaylistEditor->Playlists->Current(), **it);
					}
					else
					{
						for (; it != list.end(); ++it)
							if (Mpd.AddSong(**it) < 0)
								break;
					}
					Mpd.CommitCommandsList();
					
					if (it != list.begin() && myScreen != myPlaylistEditor)
					{
						Song &s = myPlaylist->Main()->at(myPlaylist->Main()->Size()-list.size());
						if (s.GetHash() != list[0]->GetHash())
							ShowMessage("%s", MPD::Message::PartOfSongsAdded);
					}
				}
				else
				{
					if (myScreen == myPlaylistEditor)
					{
						Mpd.AddToPlaylist(myPlaylistEditor->Playlists->Current(), path);
					}
					else
					{
						Mpd.AddSong(path);
					}
				}
				if (myScreen == myPlaylistEditor)
					myPlaylistEditor->Content->Clear(0); // make it refetch content of playlist
				FreeSongList(list);
			}
		}
		else if (Keypressed(input, Key.SeekForward) || Keypressed(input, Key.SeekBackward))
		{
			const Song *s = myPlaylist->NowPlayingSong();
			
			if (!s)
				continue;
			
			if (!s->GetTotalLength())
			{
				ShowMessage("Unknown item length!");
				continue;
			}
			LockProgressbar();
			LockStatusbar();
			
			int songpos;
			time_t t = time(NULL);
			
			songpos = Mpd.GetElapsedTime();
			
			while (Keypressed(input, Key.SeekForward) || Keypressed(input, Key.SeekBackward))
			{
				TraceMpdStatus();
				myPlaylist->UpdateTimer();
				myPlaylist->Main()->ReadKey(input);
				
				int howmuch = Config.incremental_seeking ? (myPlaylist->Timer()-t)/2+Config.seek_time : Config.seek_time;
				
				if (songpos < s->GetTotalLength() && Keypressed(input, Key.SeekForward))
				{
					songpos += howmuch;
					if (songpos > s->GetTotalLength())
						songpos = s->GetTotalLength();
				}
				if (songpos < s->GetTotalLength() && songpos > 0 && Keypressed(input, Key.SeekBackward))
				{
					songpos -= howmuch;
					if (songpos < 0)
						songpos = 0;
				}
				
				std::string tracklength;
				*wFooter << fmtBold;
				if (Config.new_design)
				{
					tracklength = Song::ShowTime(songpos);
					tracklength += "/";
					tracklength += s->GetLength();
					*wHeader << XY(0, 0) << tracklength << " ";
					wHeader->Refresh();
				}
				else
				{
					tracklength = "[" + Song::ShowTime(songpos) + "/" + s->GetLength() + "]";
					*wFooter << XY(wFooter->GetWidth()-tracklength.length(), 1) << tracklength;
				}
				double progressbar_size = songpos/double(s->GetTotalLength());
				int howlong = wFooter->GetWidth()*progressbar_size;
				
				mvwhline(wFooter->Raw(), 0, 0, 0, wFooter->GetWidth());
				mvwhline(wFooter->Raw(), 0, 0, '=',howlong);
				mvwaddch(wFooter->Raw(), 0, howlong, '>');
				*wFooter << fmtBoldEnd;
				wFooter->Refresh();
			}
			Mpd.Seek(songpos);
			UpdateStatusImmediately = 1;
			
			UnlockProgressbar();
			UnlockStatusbar();
		}
		else if (Keypressed(input, Key.ToggleDisplayMode))
		{
			if (myScreen == myPlaylist)
			{
				Config.columns_in_playlist = !Config.columns_in_playlist;
				ShowMessage("Playlist display mode: %s", Config.columns_in_playlist ? "Columns" : "Classic");
				
				if (Config.columns_in_playlist)
				{
					myPlaylist->Main()->SetItemDisplayer(Display::SongsInColumns);
					myPlaylist->Main()->SetTitle(Display::Columns());
					myPlaylist->Main()->SetGetStringFunction(Playlist::SongInColumnsToString);
				}
				else
				{
					myPlaylist->Main()->SetItemDisplayer(Display::Songs);
					myPlaylist->Main()->SetTitle("");
					myPlaylist->Main()->SetGetStringFunction(Playlist::SongToString);
				}
			}
			else if (myScreen == myBrowser)
			{
				Config.columns_in_browser = !Config.columns_in_browser;
				ShowMessage("Browser display mode: %s", Config.columns_in_browser ? "Columns" : "Classic");
				myBrowser->Main()->SetTitle(Config.columns_in_browser ? Display::Columns() : "");
				
			}
			else if (myScreen == mySearcher)
			{
				Config.columns_in_search_engine = !Config.columns_in_search_engine;
				ShowMessage("Search engine display mode: %s", Config.columns_in_search_engine ? "Columns" : "Classic");
				if (mySearcher->Main()->Size() > SearchEngine::StaticOptions)
					mySearcher->Main()->SetTitle(Config.columns_in_search_engine ? Display::Columns() : "");
			}
		}
#		ifdef HAVE_CURL_CURL_H
		else if (Keypressed(input, Key.ToggleLyricsDB))
		{
			const char *current_lyrics_plugin = Lyrics::GetPluginName(++Config.lyrics_db);
			if (!current_lyrics_plugin)
			{
				current_lyrics_plugin = Lyrics::GetPluginName(Config.lyrics_db = 0);
			}
			ShowMessage("Using lyrics database: %s", current_lyrics_plugin);
		}
#		endif // HAVE_CURL_CURL_H
		else if (Keypressed(input, Key.ToggleAutoCenter))
		{
			Config.autocenter_mode = !Config.autocenter_mode;
			ShowMessage("Auto center mode: %s", Config.autocenter_mode ? "On" : "Off");
			if (Config.autocenter_mode && myPlaylist->isPlaying() && !myPlaylist->Main()->isFiltered())
				myPlaylist->Main()->Highlight(myPlaylist->NowPlaying);
		}
		else if (Keypressed(input, Key.UpdateDB))
		{
			if (myScreen == myBrowser)
				Mpd.UpdateDirectory(locale_to_utf_cpy(myBrowser->CurrentDir()));
#			ifdef HAVE_TAGLIB_H
			else if (myScreen == myTagEditor && !Config.albums_in_tag_editor)
				Mpd.UpdateDirectory(myTagEditor->CurrentDir());
#			endif // HAVE_TAGLIB_H
			else
				Mpd.UpdateDirectory("/");
		}
		else if (Keypressed(input, Key.GoToNowPlaying))
		{
			if (myScreen == myPlaylist && myPlaylist->isPlaying())
			{
				CHECK_PLAYLIST_FOR_FILTERING;
				myPlaylist->Main()->Highlight(myPlaylist->NowPlaying);
			}
			else if (myScreen == myBrowser)
			{
				if (const Song *s = myPlaylist->NowPlayingSong())
				{
					myBrowser->LocateSong(*s);
					RedrawHeader = 1;
				}
			}
		}
		else if (Keypressed(input, Key.ToggleRepeat))
		{
			Mpd.SetRepeat(!Mpd.GetRepeat());
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.Shuffle))
		{
			Mpd.Shuffle();
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.ToggleRandom))
		{
			Mpd.SetRandom(!Mpd.GetRandom());
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.ToggleSingle))
		{
			if (myScreen == mySearcher && !mySearcher->Main()->isStatic(0))
			{
				mySearcher->Main()->Highlight(SearchEngine::SearchButton);
				mySearcher->Main()->Highlighting(0);
				mySearcher->Main()->Refresh();
				mySearcher->Main()->Highlighting(1);
				mySearcher->EnterPressed();
			}
#			ifdef HAVE_TAGLIB_H
			else if (myScreen == myTinyTagEditor)
			{
				myTinyTagEditor->Main()->Highlight(myTinyTagEditor->Main()->Size()-2); // Save
				myTinyTagEditor->EnterPressed();
			}
#			endif // HAVE_TAGLIB_H
			else
			{
				Mpd.SetSingle(!Mpd.GetSingle());
				UpdateStatusImmediately = 1;
			}
		}
		else if (Keypressed(input, Key.ToggleConsume))
		{
			Mpd.SetConsume(!Mpd.GetConsume());
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.ToggleCrossfade))
		{
			Mpd.SetCrossfade(Mpd.GetCrossfade() ? 0 : Config.crossfade_time);
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.SetCrossfade))
		{
			LockStatusbar();
			Statusbar() << "Set crossfade to: ";
			std::string crossfade = wFooter->GetString(3);
			UnlockStatusbar();
			int cf = StrToInt(crossfade);
			if (cf > 0)
			{
				Config.crossfade_time = cf;
				Mpd.SetCrossfade(cf);
				UpdateStatusImmediately = 1;
			}
		}
		else if (Keypressed(input, Key.EditTags))
		{
			if (myScreen != myBrowser || !Config.local_browser)
				CHECK_MPD_MUSIC_DIR;
#			ifdef HAVE_TAGLIB_H
			if (myTinyTagEditor->SetEdited(myScreen->CurrentSong()))
			{
				myTinyTagEditor->SwitchTo();
			}
			else if (myScreen->ActiveWindow() == myLibrary->Artists)
			{
				LockStatusbar();
				Statusbar() << fmtBold << IntoStr(Config.media_lib_primary_tag) << fmtBoldEnd << ": ";
				std::string new_tag = wFooter->GetString(myLibrary->Artists->Current());
				UnlockStatusbar();
				if (!new_tag.empty() && new_tag != myLibrary->Artists->Current())
				{
					bool success = 1;
					SongList list;
					ShowMessage("Updating tags...");
					Mpd.StartSearch(1);
					Mpd.AddSearch(Config.media_lib_primary_tag, locale_to_utf_cpy(myLibrary->Artists->Current()));
					Mpd.CommitSearch(list);
					Song::SetFunction set = IntoSetFunction(Config.media_lib_primary_tag);
					if (!set)
						continue;
					for (SongList::iterator it = list.begin(); it != list.end(); ++it)
					{
						(*it)->Localize();
						((*it)->*set)(new_tag);
						ShowMessage("Updating tags in \"%s\"...", (*it)->GetName().c_str());
						std::string path = Config.mpd_music_dir + (*it)->GetFile();
						if (!TagEditor::WriteTags(**it))
						{
							ShowMessage("Error updating tags in \"%s\"!", (*it)->GetFile().c_str());
							success = 0;
							break;
						}
					}
					if (success)
					{
						Mpd.UpdateDirectory(locale_to_utf_cpy(FindSharedDir(list)));
						ShowMessage("Tags updated succesfully!");
					}
					FreeSongList(list);
				}
			}
			else if (myScreen->ActiveWindow() == myLibrary->Albums)
			{
				LockStatusbar();
				Statusbar() << fmtBold << "Album: " << fmtBoldEnd;
				std::string new_album = wFooter->GetString(myLibrary->Albums->Current().second.Album);
				UnlockStatusbar();
				if (!new_album.empty() && new_album != myLibrary->Albums->Current().second.Album)
				{
					bool success = 1;
					ShowMessage("Updating tags...");
					for (size_t i = 0;  i < myLibrary->Songs->Size(); ++i)
					{
						(*myLibrary->Songs)[i].Localize();
						ShowMessage("Updating tags in \"%s\"...", (*myLibrary->Songs)[i].GetName().c_str());
						std::string path = Config.mpd_music_dir + (*myLibrary->Songs)[i].GetFile();
						TagLib::FileRef f(locale_to_utf_cpy(path).c_str());
						if (f.isNull())
						{
							ShowMessage("Error opening file \"%s\"!", (*myLibrary->Songs)[i].GetFile().c_str());
							success = 0;
							break;
						}
						f.tag()->setAlbum(ToWString(new_album));
						if (!f.save())
						{
							ShowMessage("Error writing tags in \"%s\"!", (*myLibrary->Songs)[i].GetFile().c_str());
							success = 0;
							break;
						}
					}
					if (success)
					{
						Mpd.UpdateDirectory(locale_to_utf_cpy(FindSharedDir(myLibrary->Songs)));
						ShowMessage("Tags updated succesfully!");
					}
				}
			}
			else if (myScreen->ActiveWindow() == myTagEditor->Dirs)
			{
				std::string old_dir = myTagEditor->Dirs->Current().first;
				LockStatusbar();
				Statusbar() << fmtBold << "Directory: " << fmtBoldEnd;
				std::string new_dir = wFooter->GetString(old_dir);
				UnlockStatusbar();
				if (!new_dir.empty() && new_dir != old_dir)
				{
					std::string full_old_dir = Config.mpd_music_dir + myTagEditor->CurrentDir() + "/" + locale_to_utf_cpy(old_dir);
					std::string full_new_dir = Config.mpd_music_dir + myTagEditor->CurrentDir() + "/" + locale_to_utf_cpy(new_dir);
					if (rename(full_old_dir.c_str(), full_new_dir.c_str()) == 0)
					{
						Mpd.UpdateDirectory(myTagEditor->CurrentDir());
					}
					else
					{
						ShowMessage("Cannot rename \"%s\" to \"%s\"!", old_dir.c_str(), new_dir.c_str());
					}
				}
			}
			else
#			endif // HAVE_TAGLIB_H
			if (myScreen == myLyrics)
			{
				myLyrics->Edit();
			}
			if (myScreen == myBrowser && myBrowser->Main()->Current().type == itDirectory)
			{
				std::string old_dir = myBrowser->Main()->Current().name;
				LockStatusbar();
				Statusbar() << fmtBold << "Directory: " << fmtBoldEnd;
				std::string new_dir = wFooter->GetString(old_dir);
				UnlockStatusbar();
				if (!new_dir.empty() && new_dir != old_dir)
				{
					std::string full_old_dir;
					if (!Config.local_browser)
						full_old_dir += Config.mpd_music_dir;
					full_old_dir += locale_to_utf_cpy(old_dir);
					std::string full_new_dir;
					if (!Config.local_browser)
						full_new_dir += Config.mpd_music_dir;
					full_new_dir += locale_to_utf_cpy(new_dir);
					int rename_result = rename(full_old_dir.c_str(), full_new_dir.c_str());
					if (rename_result == 0)
					{
						ShowMessage("\"%s\" renamed to \"%s\"", old_dir.c_str(), new_dir.c_str());
						if (!Config.local_browser)
							Mpd.UpdateDirectory(locale_to_utf_cpy(FindSharedDir(old_dir, new_dir)));
						myBrowser->GetDirectory(myBrowser->CurrentDir());
					}
					else
						ShowMessage("Cannot rename \"%s\" to \"%s\"!", old_dir.c_str(), new_dir.c_str());
				}
			}
			else if (myScreen->ActiveWindow() == myPlaylistEditor->Playlists || (myScreen == myBrowser && myBrowser->Main()->Current().type == itPlaylist))
			{
				std::string old_name = myScreen->ActiveWindow() == myPlaylistEditor->Playlists ? myPlaylistEditor->Playlists->Current() : myBrowser->Main()->Current().name;
				LockStatusbar();
				Statusbar() << fmtBold << "Playlist: " << fmtBoldEnd;
				std::string new_name = wFooter->GetString(old_name);
				UnlockStatusbar();
				if (!new_name.empty() && new_name != old_name)
				{
					Mpd.Rename(locale_to_utf_cpy(old_name), locale_to_utf_cpy(new_name));
					ShowMessage("Playlist \"%s\" renamed to \"%s\"", old_name.c_str(), new_name.c_str());
					if (!Config.local_browser && myBrowser->Main())
						myBrowser->GetDirectory("/");
					if (myPlaylistEditor->Main())
						myPlaylistEditor->Playlists->Clear(0);
				}
			}
		}
		else if (Keypressed(input, Key.GoToContainingDir))
		{
			Song *s = myScreen->CurrentSong();
			if (s)
				myBrowser->LocateSong(*s);
		}
		else if (Keypressed(input, Key.GoToPosition))
		{
			const Song *s = myPlaylist->NowPlayingSong();
			
			if (!s)
				continue;
			
			if (!s->GetTotalLength())
			{
				ShowMessage("Unknown item length!");
				continue;
			}
			LockStatusbar();
			Statusbar() << "Position to go (in %/mm:ss/seconds(s)): ";
			std::string position = wFooter->GetString();
			UnlockStatusbar();
			
			if (position.empty())
				continue;
			
			int newpos = 0;
			if (position.find(':') != std::string::npos) // probably time in mm:ss
			{
				try
				{
					newpos = StrToInt(position)*60 + StrToInt(position.substr(position.find(':')+1));
				}
				catch (std::out_of_range) { }
				if (newpos > 0 && newpos < s->GetTotalLength())
					Mpd.Seek(newpos);
				else
					ShowMessage("Out of bounds, 0:00-%s possible for mm:ss, %d given.", s->GetLength().c_str(), newpos);
			}
			else if (position.find('s') != std::string::npos) // probably position in seconds
			{
				newpos = StrToInt(position);
				if (newpos > 0 && newpos < s->GetTotalLength())
					Mpd.Seek(newpos);
				else
					ShowMessage("Out of bounds, 1-%d possible for seconds, %d given.", s->GetTotalLength(), newpos);
			}
			else
			{
				newpos = StrToInt(position);
				if (newpos > 0 && newpos < 100)
					Mpd.Seek(s->GetTotalLength()*newpos/100.0);
				else
					ShowMessage("Out of bounds, 1-99 possible for %%, %d given.", newpos);
			}
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.ReverseSelection))
		{
			if (myScreen->allowsSelection())
			{
				myScreen->ReverseSelection();
				ShowMessage("Selection reversed!");
			}
		}
		else if (Keypressed(input, Key.DeselectAll))
		{
			if (myScreen->allowsSelection())
			{
				if (myScreen->GetList()->Deselect())
				{
					ShowMessage("Items deselected!");
				}
			}
		}
		else if (Keypressed(input, Key.AddSelected))
		{
			if (!myScreen->allowsSelection())
				continue;
			
			SongList result;
			myScreen->GetSelectedSongs(result);
			
			if (result.empty())
			{
				ShowMessage("No selected items!");
				continue;
			}
			
			const size_t dialog_width = COLS*0.8;
			const size_t dialog_height = LINES*0.6;
			
			Menu<std::string> mDialog((COLS-dialog_width)/2, (LINES-dialog_height)/2, dialog_width, dialog_height, "Add selected items to...", Config.main_color, Config.window_border);
			mDialog.SetTimeout(ncmpcpp_window_timeout);
			mDialog.CyclicScrolling(Config.use_cyclic_scrolling);
			mDialog.SetItemDisplayer(Display::Generic);
			
			bool playlists_not_active = myScreen == myBrowser && Config.local_browser;
			
			if (playlists_not_active)
			{
				ShowMessage("Local items cannot be added to m3u playlist!");
			}
			
			mDialog.AddOption("Current MPD playlist");
			mDialog.AddOption("Create new playlist", 0, playlists_not_active);
			mDialog.AddSeparator();
			TagList playlists;
			Mpd.GetPlaylists(playlists);
			for (TagList::iterator it = playlists.begin(); it != playlists.end(); ++it)
			{
				utf_to_locale(*it);
				mDialog.AddOption(*it, 0, playlists_not_active);
			}
			mDialog.AddSeparator();
			mDialog.AddOption("Cancel");
			
			mDialog.Display();
			
			Playlist::BlockRefreshing = 1;
			while (!Keypressed(input, Key.Enter))
			{
				TraceMpdStatus();
				mDialog.Refresh();
				mDialog.ReadKey(input);
				
				if (Keypressed(input, Key.Up))
					mDialog.Scroll(wUp);
				else if (Keypressed(input, Key.Down))
					mDialog.Scroll(wDown);
				else if (Keypressed(input, Key.PageUp))
					mDialog.Scroll(wPageUp);
				else if (Keypressed(input, Key.PageDown))
					mDialog.Scroll(wPageDown);
				else if (Keypressed(input, Key.Home))
					mDialog.Scroll(wHome);
				else if (Keypressed(input, Key.End))
					mDialog.Scroll(wEnd);
			}
			Playlist::BlockRefreshing = 0;
			
			size_t id = mDialog.Choice();
			
			myScreen->Refresh();
			
			if (id == 0)
			{
				Mpd.StartCommandsList();
				SongList::const_iterator it = result.begin();
				for (; it != result.end(); ++it)
					if (Mpd.AddSong(**it) < 0)
						break;
				Mpd.CommitCommandsList();
				
				if (it != result.begin())
				{
					ShowMessage("Selected items added!");
					Song &s = myPlaylist->Main()->at(myPlaylist->Main()->Size()-result.size());
					if (s.GetHash() != result[0]->GetHash())
						ShowMessage("%s", MPD::Message::PartOfSongsAdded);
				}
			}
			else if (id == 1)
			{
				LockStatusbar();
				Statusbar() << "Save playlist as: ";
				std::string playlist = wFooter->GetString();
				std::string real_playlist = playlist;
				locale_to_utf(real_playlist);
				UnlockStatusbar();
				if (!playlist.empty())
				{
					Mpd.StartCommandsList();
					for (SongList::const_iterator it = result.begin(); it != result.end(); ++it)
						Mpd.AddToPlaylist(real_playlist, **it);
					Mpd.CommitCommandsList();
					ShowMessage("Selected items added to playlist \"%s\"!", playlist.c_str());
				}
			}
			else if (id > 1 && id < mDialog.Size()-1)
			{
				std::string playlist = locale_to_utf_cpy(mDialog.Current());
				Mpd.StartCommandsList();
				for (SongList::const_iterator it = result.begin(); it != result.end(); ++it)
					Mpd.AddToPlaylist(playlist, **it);
				Mpd.CommitCommandsList();
				ShowMessage("Selected items added to playlist \"%s\"!", mDialog.Current().c_str());
			}
			
			if (id != mDialog.Size()-1)
			{
				// refresh playlist's lists
				if (!Config.local_browser && myBrowser->Main() && myBrowser->CurrentDir() == "/")
					myBrowser->GetDirectory("/");
				if (myPlaylistEditor->Main())
					myPlaylistEditor->Playlists->Clear(0); // make playlist editor update itself
			}
			if (myScreen == myPlaylist)
				myPlaylist->EnableHighlighting();
			FreeSongList(result);
		}
		else if (Keypressed(input, Key.Crop))
		{
			CHECK_PLAYLIST_FOR_FILTERING;
			if (myPlaylist->Main()->hasSelected())
			{
				Mpd.StartCommandsList();
				for (int i = myPlaylist->Main()->Size()-1; i >= 0; --i)
				{
					if (!myPlaylist->Main()->isSelected(i) && i != myPlaylist->NowPlaying)
						Mpd.Delete(i);
				}
				// if mpd deletes now playing song deletion will be sluggishly slow
				// then so we have to assure it will be deleted at the very end.
				if (myPlaylist->isPlaying() && !myPlaylist->Main()->isSelected(myPlaylist->NowPlaying))
					Mpd.DeleteID(myPlaylist->NowPlayingSong()->GetID());
				
				ShowMessage("Deleting all items but selected...");
				Mpd.CommitCommandsList();
				ShowMessage("Items deleted!");
			}
			else
			{
				if (!myPlaylist->isPlaying())
				{
					ShowMessage("Nothing is playing now!");
					continue;
				}
				Mpd.StartCommandsList();
				for (int i = myPlaylist->Main()->Size()-1; i >= 0; --i)
					if (i != myPlaylist->NowPlaying)
						Mpd.Delete(i);
				ShowMessage("Deleting all items except now playing one...");
				Mpd.CommitCommandsList();
				ShowMessage("Items deleted!");
			}
		}
		else if (Keypressed(input, Key.Clear))
		{
			if (myPlaylist->Main()->isFiltered())
			{
				ShowMessage("Deleting filtered items...");
				Mpd.StartCommandsList();
				for (int i = myPlaylist->Main()->Size()-1; i >= 0; --i)
					Mpd.Delete((*myPlaylist->Main())[i].GetPosition());
				Mpd.CommitCommandsList();
				ShowMessage("Filtered items deleted!");
			}
			else
			{
				if (myScreen == myPlaylistEditor && myPlaylistEditor->Playlists->Empty())
					continue;
				int in = 0;
				if (myScreen == myPlaylistEditor)
				{
					LockStatusbar();
					Statusbar() << "Do you really want to clear playlist \"" << myPlaylistEditor->Playlists->Current() << "\" ? [y/n] ";
					curs_set(1);
					do
					{
						TraceMpdStatus();
						wFooter->ReadKey(in);
					}
					while (in != 'y' && in != 'n');
					curs_set(0);
					UnlockStatusbar();
					
					if (in == 'y')
					{
						Mpd.ClearPlaylist(locale_to_utf_cpy(myPlaylistEditor->Playlists->Current()));
						myPlaylistEditor->Content->Clear(0);
					}
					else
						ShowMessage("Aborted!");
				}
				else
				{
					ShowMessage("Clearing playlist...");
					Mpd.ClearPlaylist();
				}
				if (myScreen != myPlaylistEditor || in == 'y')
					ShowMessage("Playlist cleared!");
			}
			// if playlist is cleared, items list have to be updated, but this
			// can be blocked if new song was added to playlist less than one
			// second ago, so we need to assume it's unlocked.
			BlockItemListUpdate = 0;
			UpdateStatusImmediately = 1;
		}
		else if (Keypressed(input, Key.SortPlaylist) && myScreen == myPlaylist)
		{
			CHECK_PLAYLIST_FOR_FILTERING;
			myPlaylist->Sort();
			myPlaylist->EnableHighlighting();
		}
		else if (Keypressed(input, Key.ApplyFilter))
		{
			List *mList = myScreen->GetList();
			
			if (!mList)
				continue;
			
			LockStatusbar();
			Statusbar() << fmtBold << "Apply filter: " << fmtBoldEnd;
			wFooter->SetGetStringHelper(StatusbarApplyFilterImmediately);
			wFooter->GetString(mList->GetFilter());
			wFooter->SetGetStringHelper(StatusbarGetStringHelper);
			UnlockStatusbar();
			
			if (mList->isFiltered())
				ShowMessage("Using filter \"%s\"", mList->GetFilter().c_str());
			else
				ShowMessage("Filtering disabled");
			
			if (myScreen == myPlaylist)
			{
				myPlaylist->EnableHighlighting();
				Playlist::ReloadTotalLength = 1;
				RedrawHeader = 1;
			}
		}
		else if (Keypressed(input, Key.FindForward) || Keypressed(input, Key.FindBackward))
		{
			List *mList = myScreen->GetList();
			
			if (mList)
			{
				LockStatusbar();
				Statusbar() << "Find " << (Keypressed(input, Key.FindForward) ? "forward" : "backward") << ": ";
				std::string findme = wFooter->GetString();
				UnlockStatusbar();
				myPlaylist->UpdateTimer();
				
				if (!findme.empty())
					ShowMessage("Searching...");
				
				bool success = mList->Search(findme, myScreen == mySearcher ? SearchEngine::StaticOptions : 0, REG_ICASE | Config.regex_type);
				
				if (findme.empty())
					continue;
				
				success ? ShowMessage("Searching finished!") : ShowMessage("Unable to find \"%s\"", findme.c_str());
				
				if (Keypressed(input, Key.FindForward))
					mList->NextFound(Config.wrapped_search);
				else
					mList->PrevFound(Config.wrapped_search);
				
				if (myScreen == myPlaylist)
					myPlaylist->EnableHighlighting();
			}
			else if (myScreen == myHelp || myScreen == myLyrics || myScreen == myInfo)
			{
				LockStatusbar();
				Statusbar() << "Find: ";
				std::string findme = wFooter->GetString();
				UnlockStatusbar();
				
				ShowMessage("Searching...");
				Screen<Scrollpad> *s = static_cast<Screen<Scrollpad> *>(myScreen);
				s->Main()->RemoveFormatting();
				ShowMessage("%s", s->Main()->SetFormatting(fmtReverse, findme, fmtReverseEnd) || findme.empty() ? "Done!" : "No matching patterns found");
				s->Main()->Flush();
			}
		}
		else if (Keypressed(input, Key.NextFoundPosition) || Keypressed(input, Key.PrevFoundPosition))
		{
			List *mList = myScreen->GetList();
			
			if (!mList)
				continue;
			
			if (Keypressed(input, Key.NextFoundPosition))
				mList->NextFound(Config.wrapped_search);
			else
				mList->PrevFound(Config.wrapped_search);
		}
		else if (Keypressed(input, Key.ToggleFindMode))
		{
			Config.wrapped_search = !Config.wrapped_search;
			ShowMessage("Search mode: %s", Config.wrapped_search ? "Wrapped" : "Normal");
		}
		else if (Keypressed(input, Key.ToggleSpaceMode))
		{
			Config.space_selects = !Config.space_selects;
			ShowMessage("Space mode: %s item", Config.space_selects ? "Select/deselect" : "Add");
		}
		else if (Keypressed(input, Key.ToggleAddMode))
		{
			Config.ncmpc_like_songs_adding = !Config.ncmpc_like_songs_adding;
			ShowMessage("Add mode: %s", Config.ncmpc_like_songs_adding ? "Add item to playlist, remove if already added" : "Always add item to playlist");
		}
		else if (Keypressed(input, Key.ToggleMouse))
		{
			Config.mouse_support = !Config.mouse_support;
			mousemask(Config.mouse_support ? ALL_MOUSE_EVENTS : 0, 0);
			ShowMessage("Mouse support %s", Config.mouse_support ? "enabled" : "disabled");
		}
		else if (Keypressed(input, Key.SwitchTagTypeList))
		{
			if (myScreen == myPlaylist)
			{
				LockStatusbar();
				Statusbar() << "Number of random songs: ";
				size_t number = StrToLong(wFooter->GetString());
				UnlockStatusbar();
				if (number && Mpd.AddRandomSongs(number))
					ShowMessage("%zu random song%s added to playlist!", number, number == 1 ? "" : "s");
			}
			else if (myScreen == myBrowser)
			{
				myBrowser->ChangeBrowseMode();
			}
			else if (myScreen->ActiveWindow() == myLibrary->Artists
			||	 (myLibrary->Columns() == 2 && myScreen->ActiveWindow() == myLibrary->Albums))
			{
				LockStatusbar();
				Statusbar() << "Tag type ? [" << fmtBold << 'a' << fmtBoldEnd << "rtist/" << fmtBold << 'y' << fmtBoldEnd << "ear/" << fmtBold << 'g' << fmtBoldEnd << "enre/" << fmtBold << 'c' << fmtBoldEnd << "omposer/" << fmtBold << 'p' << fmtBoldEnd << "erformer] ";
				int item;
				curs_set(1);
				do
				{
					TraceMpdStatus();
					wFooter->ReadKey(item);
				}
				while (item != 'a' && item != 'y' && item != 'g' && item != 'c' && item != 'p');
				curs_set(0);
				UnlockStatusbar();
				mpd_TagItems new_tagitem = IntoTagItem(item);
				if (new_tagitem != Config.media_lib_primary_tag)
				{
					Config.media_lib_primary_tag = new_tagitem;
					std::string item_type = IntoStr(Config.media_lib_primary_tag);
					myLibrary->Artists->SetTitle(item_type + "s");
					myLibrary->Artists->Reset();
					ToLower(item_type);
					if (myLibrary->Columns() == 2)
					{
						myLibrary->Songs->Clear(0);
						myLibrary->Albums->Reset();
						myLibrary->Albums->Clear();
						myLibrary->Albums->SetTitle("Albums (sorted by " + item_type + ")");
						myLibrary->Albums->Display();
					}
					else
					{
						myLibrary->Artists->Clear(0);
						myLibrary->Artists->Display();
					}
					ShowMessage("Switched to list of %s tag", item_type.c_str());
				}
			}
		}
		else if (Keypressed(input, Key.SongInfo))
		{
			myInfo->GetSong();
		}
#		ifdef HAVE_CURL_CURL_H
		else if (Keypressed(input, Key.ArtistInfo))
		{
			myInfo->GetArtist();
		}
#		endif // HAVE_CURL_CURL_H
		else if (Keypressed(input, Key.Lyrics))
		{
			myLyrics->SwitchTo();
		}
		else if (Keypressed(input, Key.Quit))
		{
			main_exit = 1;
		}
#		ifdef HAVE_TAGLIB_H
		else if (myScreen == myTinyTagEditor)
		{
			continue;
		}
#		endif // HAVE_TAGLIB_H
		else if (Keypressed(input, Key.Help))
		{
			myHelp->SwitchTo();
		}
		else if (Keypressed(input, Key.ScreenSwitcher))
		{
			if (myScreen == myPlaylist)
				myBrowser->SwitchTo();
			else
				myPlaylist->SwitchTo();
		}
		else if (Keypressed(input, Key.Playlist))
		{
			myPlaylist->SwitchTo();
		}
		else if (Keypressed(input, Key.Browser))
		{
			myBrowser->SwitchTo();
		}
		else if (Keypressed(input, Key.SearchEngine))
		{
			mySearcher->SwitchTo();
		}
		else if (Keypressed(input, Key.MediaLibrary))
		{
			myLibrary->SwitchTo();
		}
		else if (Keypressed(input, Key.PlaylistEditor))
		{
			myPlaylistEditor->SwitchTo();
		}
#		ifdef HAVE_TAGLIB_H
		else if (Keypressed(input, Key.TagEditor))
		{
			CHECK_MPD_MUSIC_DIR;
			myTagEditor->SwitchTo();
		}
#		endif // HAVE_TAGLIB_H
#		ifdef ENABLE_OUTPUTS
		else if (Keypressed(input, Key.Outputs))
		{
			myOutputs->SwitchTo();
		}
#		endif // ENABLE_OUTPUTS
#		ifdef ENABLE_CLOCK
		else if (Keypressed(input, Key.Clock))
		{
			myClock->SwitchTo();
		}
#		endif // ENABLE_CLOCK
		// key mapping end
	}
	// restore old cerr buffer
	std::cerr.rdbuf(cerr_buffer);
	errorlog.close();
	Mpd.Disconnect();
	DestroyScreen();
	WindowTitle("");
	return 0;
}

