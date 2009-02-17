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

#ifndef _MPDPP_H
#define _MPDPP_H

#include <vector>

#include "libmpdclient.h"
#include "song.h"

namespace MPD
{
	namespace Message
	{
		extern const char *PartOfSongsAdded;
		extern const char *FullPlaylist;
	}
	
	enum QueueCommandType { qctAdd, qctAddToPlaylist, qctDelete, qctDeleteID, qctMove, qctPlaylistMove, qctDeleteFromPlaylist };
	enum ItemType { itDirectory, itSong, itPlaylist };
	enum PlayerState { psUnknown, psStop, psPlay, psPause };

	struct Item
	{
		Item() : song(0) { }
		Song *song;
		ItemType type;
		std::string name;
	};
	
	struct StatusChanges
	{
		StatusChanges() : Playlist(0), SongID(0), Database(0), DBUpdating(0), Volume(0), ElapsedTime(0), Crossfade(0), Random(0), Repeat(0), PlayerState(0), StatusFlags(0) { }
		bool Playlist:1;
		bool SongID:1;
		bool Database:1;
		bool DBUpdating:1;
		bool Volume:1;
		bool ElapsedTime:1;
		bool Crossfade:1;
		bool Random:1;
		bool Repeat:1;
		bool PlayerState:1;
		bool StatusFlags:1;
	};
	
	typedef std::vector<Item> ItemList;
	typedef std::vector<Song *> SongList;
	typedef std::vector<std::string> TagList;

	void FreeSongList(SongList &);
	void FreeItemList(ItemList &);

	class Connection
	{
		struct QueueCommand
		{
			QueueCommand() : id(0), id2(0) { }
			QueueCommandType type;
			std::string playlist_path;
			std::string item_path;
			int id;
			int id2;
		};
		
		typedef void (*StatusUpdater) (Connection *, StatusChanges, void *);
		typedef void (*ErrorHandler) (Connection *, int, const char *, void *);
		
		public:
			Connection();
			~Connection();
			
			bool Connect();
			bool Connected() const;
			void Disconnect();
			
			const std::string & GetHostname() { return itsHost; }
			int GetPort() { return itsPort; }
			
			float Version() const;
			
			void SetHostname(const std::string &);
			void SetPort(int port) { itsPort = port; }
			void SetTimeout(int timeout) { itsTimeout = timeout; }
			void SetPassword(const std::string &password) { itsPassword = password; }
			void SendPassword() const;
			
			void SetStatusUpdater(StatusUpdater, void *);
			void SetErrorHandler(ErrorHandler, void *);
			void UpdateStatus();
			void UpdateDirectory(const std::string &) const;
			
			void Execute(const std::string &) const;
			
			void Play() const;
			void Play(int) const;
			void PlayID(int) const;
			void Pause() const;
			void Stop() const;
			void Next() const;
			void Prev() const;
			void Move(int, int) const;
			void Seek(int) const;
			void Shuffle() const;
			void ClearPlaylist() const;
			
			PlayerState GetState() const { return isConnected && itsCurrentStatus ? (PlayerState)itsCurrentStatus->state : psUnknown; }
			bool GetRepeat() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->repeat : 0; }
			bool GetRandom() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->random : 0; }
			bool GetDBIsUpdating() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->updatingDb : 0; }
			int GetVolume() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->volume : -1; }
			int GetCrossfade() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->crossfade : -1; }
			long long GetPlaylistID() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->playlist : -1; }
			long long GetOldPlaylistID() const { return isConnected && itsOldStatus ? itsOldStatus->playlist : -1; }
			int GetElapsedTime() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->elapsedTime : -1; }
			
			size_t GetMaxPlaylistLength() const { return itsMaxPlaylistLength; }
			size_t GetPlaylistLength() const { return isConnected && itsCurrentStatus ? itsCurrentStatus->playlistLength : 0; }
			void GetPlaylistChanges(long long, SongList &) const;
			
			const std::string & GetErrorMessage() const { return itsErrorMessage; }
			int GetErrorCode() const { return itsErrorCode; }
			
			Song GetCurrentSong() const;
			int GetCurrentSongPos() const;
			Song GetSong(const std::string &) const;
			void GetPlaylistContent(const std::string &, SongList &) const;
			
			void SetRepeat(bool) const;
			void SetRandom(bool) const;
			void SetVolume(int) const;
			void SetCrossfade(int) const;
			
			int AddSong(const std::string &); // returns id of added song
			int AddSong(const Song &); // returns id of added song
			void QueueAddSong(const std::string &);
			void QueueAddSong(const Song &);
			void QueueAddToPlaylist(const std::string &, const std::string &);
			void QueueAddToPlaylist(const std::string &, const Song &);
			void QueueDeleteSong(int);
			void QueueDeleteSongId(int);
			void QueueMove(int, int);
			void QueueMove(const std::string &, int, int);
			void QueueDeleteFromPlaylist(const std::string &, int);
			bool CommitQueue();
			
			void DeletePlaylist(const std::string &) const;
			bool SavePlaylist(const std::string &) const;
			void ClearPlaylist(const std::string &) const;
			void AddToPlaylist(const std::string &, const Song &) const;
			void AddToPlaylist(const std::string &, const std::string &) const;
			void Move(const std::string &, int, int) const;
			void Rename(const std::string &, const std::string &) const;
			
			void StartSearch(bool) const;
			void StartFieldSearch(mpd_TagItems);
			void AddSearch(mpd_TagItems, const std::string &) const;
			void CommitSearch(SongList &) const;
			void CommitSearch(TagList &) const;
			
			void GetPlaylists(TagList &) const;
			void GetList(TagList &, mpd_TagItems) const;
			void GetArtists(TagList &) const;
			void GetAlbums(std::string, TagList &) const;
			void GetDirectory(const std::string &, ItemList &) const;
			void GetDirectoryRecursive(const std::string &, SongList &) const;
			void GetSongs(const std::string &, SongList &) const;
			void GetDirectories(const std::string &, TagList &) const;
			
		private:
			void ClearQueue();
			int CheckForErrors();
			
			mpd_Connection *itsConnection;
			bool isConnected;
			
			std::string itsErrorMessage;
			int itsErrorCode;
			size_t itsMaxPlaylistLength;
			
			std::string itsHost;
			int itsPort;
			int itsTimeout;
			std::string itsPassword;
			
			mpd_Stats *itsOldStats;
			mpd_Stats *itsCurrentStats;
			mpd_Status *itsCurrentStatus;
			mpd_Status *itsOldStatus;
			
			StatusChanges itsChanges;
			
			StatusUpdater itsUpdater;
			void *itsStatusUpdaterUserdata;
			ErrorHandler itsErrorHandler;
			void *itsErrorHandlerUserdata;
			
			mpd_TagItems itsSearchedField;
			std::vector<QueueCommand *> itsQueue;
	};
}

#endif

