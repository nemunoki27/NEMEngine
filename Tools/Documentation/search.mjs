// 日本語の単語と英語APIの成分を同じ規則で索引化する
export function tokenize(text) {

	const words = Array.from(new Intl.Segmenter('ja', { granularity: 'word' }).segment(text.normalize('NFKC')));
	return words.filter(word => word.isWordLike).flatMap(word =>
		[word.segment.toLowerCase(), ...word.segment.replace(/([a-z])([A-Z])/g, '$1 $2').toLowerCase().split(/[^\p{L}\p{N}_]+/u)]);
}
