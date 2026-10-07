// SPDX-FileCopyrightText: 2026 Erich Seifert <dev@erichseifert.de>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "postproject_resolver.h"

#include <postproject/postproject.hpp>

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require (bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error (message);
	}
}

void write_u16 (std::ostream& output, std::uint16_t value)
{
	output.put (static_cast<char> (value & 0xff));
	output.put (static_cast<char> ((value >> 8) & 0xff));
}

void write_u32 (std::ostream& output, std::uint32_t value)
{
	write_u16 (output, static_cast<std::uint16_t> (value & 0xffff));
	write_u16 (output, static_cast<std::uint16_t> (value >> 16));
}

void write_stereo_wave (const std::filesystem::path& path)
{
	constexpr std::uint32_t sample_rate = 48000;
	constexpr std::uint16_t channels = 2;
	constexpr std::uint16_t bits_per_sample = 16;
	constexpr std::uint32_t frames = sample_rate;
	constexpr std::uint32_t data_size = frames * channels * (bits_per_sample / 8);

	std::filesystem::create_directories (path.parent_path ());
	std::ofstream output (path, std::ios::binary | std::ios::trunc);
	output.write ("RIFF", 4);
	write_u32 (output, 36 + data_size);
	output.write ("WAVEfmt ", 8);
	write_u32 (output, 16);
	write_u16 (output, 1);
	write_u16 (output, channels);
	write_u32 (output, sample_rate);
	write_u32 (output, sample_rate * channels * (bits_per_sample / 8));
	write_u16 (output, channels * (bits_per_sample / 8));
	write_u16 (output, bits_per_sample);
	output.write ("data", 4);
	write_u32 (output, data_size);
	for (std::uint32_t frame = 0; frame < frames; ++frame) {
		const auto sample = static_cast<std::int16_t> ((frame % 200) * 120 - 12000);
		write_u16 (output, static_cast<std::uint16_t> (sample));
		write_u16 (output, static_cast<std::uint16_t> (-sample));
	}
	require (static_cast<bool> (output), "write stereo WAV");
}

} // namespace

int main (int argc, char** argv)
try {
	require (argc == 2, "usage: resolver-scenario WORK_DIRECTORY");
	static_assert (POSTPROJECT_HAS_EXCEPTIONS == 1, "pilot requires C++ exceptions");

	const auto root = std::filesystem::absolute (argv[1]);
	const auto production_path = root / "audio.pproj";
	const auto original = root / "session" / "take-01.wav";
	const auto recovered = root / "recovered" / "renamed-take.wav";
	const auto duplicate = root / "duplicate" / "another-name.wav";
	write_stereo_wave (original);
	postproject::Production::create (production_path.string (), "Audio pilot").value ();

	ArdourPostProject::record_source (production_path.string (), "source-42", original.string ());
	std::filesystem::create_directories (recovered.parent_path ());
	std::filesystem::rename (original, recovered);

	const auto match = ArdourPostProject::resolve_missing_source (
	    production_path.string (), "source-42", {recovered.parent_path ().string ()});
	require (match && std::filesystem::equivalent (*match, recovered), "resolve renamed stereo audio by content");

	std::filesystem::create_directories (duplicate.parent_path ());
	std::filesystem::copy_file (recovered, duplicate);
	const auto ambiguous = ArdourPostProject::resolve_missing_source (
	    production_path.string (), "source-42",
	    {recovered.parent_path ().string (), duplicate.parent_path ().string ()});
	require (!ambiguous, "do not choose between content-identical audio files");
	std::filesystem::remove (duplicate);
	ArdourPostProject::record_source (production_path.string (), "source-42", recovered.string ());
	const auto known = ArdourPostProject::resolve_missing_source (
	    production_path.string (), "source-42", {});
	require (known && std::filesystem::equivalent (*known, recovered),
	         "use a content-verified locator confirmed by a later save");

	auto production = postproject::Production::open (production_path.string ()).value ();
	const auto objects = production.findByExternalIdentifier ("org.ardour:source_id", "source-42").value ();
	require (objects.size () == 1 && objects.front ().kind () == postproject::ObjectKind::asset,
	         "attach the Ardour source identifier to one asset");
	const auto before_facts = production.latestRevision ().value ();
	const auto modified = std::filesystem::last_write_time (recovered);
	std::filesystem::last_write_time (recovered, modified + std::chrono::seconds (5));
	ArdourPostProject::record_source (production_path.string (), "source-42", recovered.string ());
	const auto facts_revision = production.latestRevision ().value ();
	require (before_facts && facts_revision && facts_revision->sequence == before_facts->sequence + 1,
	         "a repeated save records changed file facts without changing content");
	const auto facts_events = production.revisionEvents (facts_revision->id).value ();
	require (facts_events.size () == 1 &&
	         std::holds_alternative<postproject::ResourceFileFactsObservedEvent> (facts_events.front ().payload),
	         "the save owns its file-facts journal event");
	const auto revision = production.latestRevision ().value ();
	require (revision && revision->origin && revision->origin->name == "org.ardour",
	         "record Ardour as revision origin");

	bool caught = false;
	try {
		postproject::Production::open ((root / "absent.pproj").string ()).value ();
	} catch (const postproject::Exception&) {
		caught = true;
	}
	require (caught, "translate a PostProject Result through the exception-enabled projection");

	const auto asset_id = objects.front ().assetId ().value ();
	{
		auto edit = production.beginTransaction ().value ();
		edit.addRepresentation (asset_id, postproject::RepresentationKind::original,
		                        postproject::MediaSource (recovered.string ())).value ();
		const auto receipt = edit.commit ().value ();
		require (receipt.revision.has_value (), "commit a distinct second original");
	}
	const auto before_ambiguity = production.latestRevision ().value ();
	for (bool save : {false, true}) {
		bool refused = false;
		try {
			if (save) {
				ArdourPostProject::record_source (production_path.string (), "source-42", recovered.string ());
			} else {
				ArdourPostProject::resolve_missing_source (production_path.string (), "source-42", {});
			}
		} catch (const postproject::Exception& error) {
			refused = error.error ().code () == postproject::ErrorCode::ambiguous_resolution;
		}
		require (refused, "save and resolve refuse several original representations");
	}
	require (production.latestRevision ().value ()->id == before_ambiguity->id,
	         "ambiguous decisions do not mutate the production");

	std::cout << "resolved renamed 48 kHz stereo audio without choosing an ambiguity\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what () << '\n';
	return 1;
}
