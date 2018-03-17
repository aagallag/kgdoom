-- kgsws' Lua Doom exports
-- Doom monsters

PIT_KeenOpen =
function(sector)
	sector.GenericCeiling(sector.FindLowestCeiling() - 4, 2, 0, "dsdoropn")
end

PIT_EggCheck =
function(thing, info)
	if thing.info == MT_INVULN then
		return false, true
	end
end

a.KeenDie =
function(mobj)
	if not globalThingsIterator(PIT_BossDeath, mobj.info) then
		if game.map == "MAP32" and mobj.attacker.info == MT_CYBORG and globalThingsIterator(PIT_EggCheck) then
			-- easter egg
			game.Exit("D2SECRET")
		else
			-- normal exit
			sectorTagIterator(666, PIT_KeenOpen)
		end
	end
end

-- MT_KEEN
mtype = {
	painSound = "dskeenpn",
	deathSound = "dskeendt",
	ednum = 72,
	health = 100,
	radius = 16,
	height = 72,
	reactionTime = 8,
	painChance = 256,
	damageScale = {0},
	__Monster = true,
	__spawnCeiling = true,
	__noGravity = true,
	_spawn = {
		{"KEENA", -1}
	},
	_pain = {
		{"KEENM", 4},
		{"KEENM", 8, a.SoundPain},
		"_spawn"
	},
	_death = {
		{"KEENA", 6},
		{"KEENB", 6},
		{"KEENC", 6, a.SoundDeath},
		{"KEEND", 6},
		{"KEENE", 6},
		{"KEENF", 6},
		{"KEENG", 6},
		{"KEENH", 6},
		{"KEENI", 6},
		{"KEENJ", 6},
		{"KEENK", 6, a.Fall},
		{"KEENL", -1, a.KeenDie},
	},
	_crush = {
		{"POL5A0", -1, a.Crushed}
	}
}
MT_KEEN = createMobjType(mtype)

