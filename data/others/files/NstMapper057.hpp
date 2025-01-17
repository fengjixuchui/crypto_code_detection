////////////////////////////////////////////////////////////////////////////////////////
//
// Nestopia - NES/Famicom emulator written in C++
//
// Copyright (C) 2003-2007 Martin Freij
//
// This file is part of Nestopia.
//
// Nestopia is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Nestopia is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Nestopia; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
////////////////////////////////////////////////////////////////////////////////////////

#ifndef NST_MAPPER_57_H
#define NST_MAPPER_57_H

#ifdef NST_PRAGMA_ONCE
#pragma once
#endif

namespace Nes
{
	namespace Core
	{
		class Mapper57 : public Mapper
		{
		public:

			explicit Mapper57(Context&);

		private:

			~Mapper57();

			class CartSwitches;

			enum Attribute
			{
				ATR_103 = 1,
				ATR_GK_54,
				ATR_GK_L01A,
				ATR_GK_L02A,
				ATR_GK_47
			};

			void SubReset(bool);
			void SubSave(State::Saver&) const;
			void SubLoad(State::Loader&);
			Device QueryDevice(DeviceType);
			void UpdateChr() const;

			NES_DECL_PEEK( 6000 );
			NES_DECL_POKE( 8000 );
			NES_DECL_POKE( 8800 );

			uint regs[2];
			CartSwitches* const cartSwitches;
		};
	}
}

#endif
