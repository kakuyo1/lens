pragma Singleton

import QtQuick

/**
 * The card the settings preview draws, shaped into the payload Bubble.show() takes (UI-SPEC.md 4.4).
 *
 * The preview is the explanation bubble itself, so what a reader resting on "Etymology" sees is the
 * card their own bubble would be: the definition in the explanation language they have set, in the
 * fields the controller fills for that language, at the pace they have set. One part is forced in --
 * the block the setting under the pointer adds -- because a preview that mirrored the settings
 * exactly would draw nothing at all for a switch that is off, and the hover asks what turning it on
 * would give.
 *
 * The sample is content rather than interface copy, so it lives in data/preview.json and not in a
 * translation table: the language of an explanation is a runtime setting, not the interface's.
 */
QtObject {
    /// @param key Which setting is being previewed: "speed", "language", "multisense", "etymology",
    ///        "example", "ipa", "signpost", "collocations", "font" or "fontSize".
    /// @param language For the "language" preview, the language the hovered row carries, which
    ///        this card is drawn in instead of the stored one.
    /// @return The payload for Bubble.show(), or null when there is no sample to draw -- a machine
    ///         whose data/preview.json could not be read has no preview rather than an empty card.
    function payload(key, language) {
        const sample = Controller.settingPreview;
        if (!sample || !sample.word)
            return null;
        // Read once: Controller.settings rebuilds the whole map on every read of it.
        const settings = Controller.settings;
        // The font preview is drawn in English whatever the explanation language is, since English
        // is what the font setting is about: a card in the reader's own language would show the
        // setting leaving that language exactly as it found it. The size preview is drawn in
        // English for the same reason once removed: the English definition is the longest run on
        // the card, so it is the one that shows what a size does to the card's height.
        const lang = key === "font" || key === "fontSize" ? "en"
                   : key === "language" && language ? language
                   : (settings.explanationLang || "en");
        /// The sample's text in one language, falling back to the English it is always written in.
        const local = (text) => (text ? (text[lang] || text.en || "") : "");
        // The one part the hovered row forces: several senses, an origin, an example sentence, an
        // IPA, a signpost or a collocation line. Everything else follows the stored switches.
        const multiple = key === "multisense" || settings.multiSense;
        const senses = multiple ? sample.senses || [] : [];
        const payload = {
            title: sample.word,
            type: "word",
            ipa: key === "ipa" || settings.ipa ? sample.ipa || "" : "",
            // No verdict: the demo word is not a word the reader has met, and the tag row says so.
            status: "",
            en: "",
            zh: "",
            translation: "",
            // One language a sense, the rule the duty draws the card by rather than this file's
            // (explanation_duty.cpp): the English definition in the English field when English was
            // asked for, the reader's own language otherwise, never both.
            senses: senses.map((sense) => ({en: lang === "en" ? sense.en || "" : "",
                                            text: lang === "en" ? "" : local(sense)})),
            signpost: key === "signpost" || settings.signpost ? sample.signpost || "" : "",
            collocations: key === "collocations" || settings.collocations ? sample.collocations || [] : [],
            etymology: key === "etymology" || settings.etymology ? local(sample.etymology) : "",
            example: key === "example" || settings.example ? sample.example || "" : "",
            // No clock: this card goes when the pointer leaves the title, not when a count runs out,
            // and the countdown line is one of the two things only a real bubble carries.
            countsDown: false
        };
        // The definition line is the one explanation language, in the field the controller fills for
        // it: English and Chinese in their own, every other language in the translation (UI-SPEC.md 4.3).
        if (lang === "zh")
            payload.zh = local(sample.definition);
        else if (lang === "en")
            payload.en = local(sample.definition);
        else
            payload.translation = local(sample.definition);
        return payload;
    }
}
